/*
 * Copyright (c) 2018, aetf <aetf at unlimitedcodeworks dot xyz>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice,
 *       this list of conditions and the following disclaimer in the documentation
 *       and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "spotlightprovider.h"

#include "scoped_guard.h"

#include <KIO/StoredTransferJob>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QMetaMethod>
#include <QUrlQuery>

SpotlightProvider::SpotlightProvider(QObject *parent, const QVariantList &args)
    : PotdProvider(parent, args)
{
    const auto url = SpotlightParser::buildUrl();

    auto job = KIO::storedGet(url, KIO::NoReload, KIO::HideProgressInfo);
    connect(job, &KIO::StoredTransferJob::finished, this, &SpotlightProvider::pageRequestFinished);
}

SpotlightProvider::~SpotlightProvider() = default;

QImage SpotlightProvider::image() const
{
    return m_image;
}

void SpotlightProvider::pageRequestFinished(KJob *_job)
{
    scope_guard emit_error([this]() { emit error(this); });

    auto job = dynamic_cast<KIO::StoredTransferJob *>(_job);
    if (job->error()) {
        return;
    }

    const auto imageItem = SpotlightParser::parseReply(job->data());
    const auto imageUrl = SpotlightParser::extractImageUrl(imageItem);

    auto imageJob = KIO::storedGet(imageUrl, KIO::NoReload, KIO::HideProgressInfo);
    connect(imageJob, &KIO::StoredTransferJob::finished, this, &SpotlightProvider::imageRequestFinished);
    emit_error.dismiss();
}

void SpotlightProvider::imageRequestFinished(KJob *_job)
{
    scope_guard emit_error([this]() { emit error(this); });

    auto job = dynamic_cast<KIO::StoredTransferJob *>(_job);
    if (job->error()) {
        return;
    }

    m_image = QImage::fromData(job->data());
    if (m_image.isNull()) {
        return;
    }

    emit_error.dismiss();

    auto finishSignal = QMetaMethod::fromSignal(&SpotlightProvider::finished);
    if (!isSignalConnected(finishSignal)) {
        qCritical() << "No listener found for the finished signal on SpotlightProvider, probably a data race "
                       "in Potd data engine";
    }
    emit finished(this);
}

QString SpotlightParser::getCountryLetters(const QLocale &locale)
{
    return locale.name().split('_').at(1).toLower();
}

QUrl SpotlightParser::buildUrl()
{
    QUrl url(QLatin1Literal("https://arc.msn.com/v3/Delivery/Placement"));
    QUrlQuery query;
    // Purpose unknown, must be this value,
    query.addQueryItem(QLatin1Literal("pid"), QLatin1Literal("209567"));
    // Output format
    query.addQueryItem(QLatin1Literal("fmt"), QLatin1Literal("json"));
    // Purpose unknown, must be this value,
    query.addQueryItem(QLatin1Literal("cdm"), QLatin1Literal("1"));
    // Language
    query.addQueryItem(QLatin1Literal("lc"), QLatin1Literal("en,en-US"));
    // Country
    query.addQueryItem(QLatin1Literal("ctry"), QLatin1Literal("US"));

    url.setQuery(query);

    return url;
}

QString SpotlightParser::parseReply(QByteArray ba)
{
    auto json = QJsonDocument::fromJson(ba);
    auto imageItemStr = json[QLatin1Literal("batchrsp")][QLatin1Literal("items")][0][QLatin1Literal("item")];
    if (!imageItemStr.isString()) {
        return {};
    }

    return imageItemStr.toString();
}

QUrl SpotlightParser::extractImageUrl(QString s)
{
    auto imageItem = QJsonDocument::fromJson(s.toUtf8());

    // TODO: detect landscape or portrait
    //    constexpr const auto keyP = QLatin1Literal("image_fullscreen_001_portrait");
    constexpr const auto keyL = QLatin1Literal("image_fullscreen_001_landscape");
    auto imageUrl = imageItem[QLatin1Literal("ad")][keyL][QLatin1Literal("u")];
    if (!imageUrl.isString() || imageUrl.toString().isEmpty()) {
        return {};
    }

    return QUrl(imageUrl.toString());
}
