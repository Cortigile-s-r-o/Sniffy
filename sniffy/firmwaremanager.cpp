#include "firmwaremanager.h"
#include "graphics/graphics.h"
#include "customsettings.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>

namespace {

constexpr auto kRequestKindProperty = "sniffyRequestKind";
constexpr auto kUidHexProperty = "sniffyUidHex";
constexpr auto kMcuProperty = "sniffyMcu";
constexpr auto kManifestRequestKind = "latestManifest";
constexpr auto kBinaryRequestKind = "firmwareBinary";

QString extractReplyFilename(QNetworkReply *reply)
{
    QString filename;
    const QVariant contentDisposition = reply->header(QNetworkRequest::ContentDispositionHeader);
    const QString contentDispositionText = contentDisposition.toString();
    if (!contentDispositionText.isEmpty())
    {
        const int nameIndex = contentDispositionText.indexOf("filename=");
        if (nameIndex != -1)
        {
            filename = contentDispositionText.mid(nameIndex + 9);
            filename = filename.split(';').first();
            filename = filename.replace('"', QString()).trimmed();
        }
    }

    if (filename.isEmpty())
    {
        filename = reply->url().fileName();
    }

    if (filename.isEmpty())
    {
        filename = QStringLiteral("unknown_firmware.bin");
    }

    return filename;
}

QString extractJsonErrorMessage(const QByteArray &data)
{
    if (!data.trimmed().startsWith('{'))
    {
        return QString();
    }

    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (!document.isObject())
    {
        return QString();
    }

    const QString errorMessage = document.object().value(QStringLiteral("error")).toString();
    if (errorMessage.isEmpty())
    {
        return QString();
    }

    return errorMessage.length() > 150 ? errorMessage.left(150) + QStringLiteral("...") : errorMessage;
}

int compareFirmwareVersions(const QString &leftVersion, const QString &rightVersion)
{
    const QVersionNumber left = QVersionNumber::fromString(leftVersion.trimmed());
    const QVersionNumber right = QVersionNumber::fromString(rightVersion.trimmed());
    if (!left.isNull() && !right.isNull())
    {
        return QVersionNumber::compare(left, right);
    }

    return QString::compare(leftVersion.trimmed(), rightVersion.trimmed(), Qt::CaseInsensitive);
}

} // namespace

FirmwareManager::FirmwareManager(Authenticator *auth, QObject *parent) : QObject(parent),
                                                    m_flashInProgress(false)
{
    // Initialize Flasher
    m_flasher = new StLinkFlasher();
    m_flasherThread = new QThread(this);
    m_flasher->moveToThread(m_flasherThread);
    m_flasherThread->start();

    connect(m_flasher, &StLinkFlasher::progressChanged, this, &FirmwareManager::onFlashProgress);
    connect(m_flasher, &StLinkFlasher::logMessage, this, &FirmwareManager::onFlashLog);
    connect(m_flasher, &StLinkFlasher::operationFinished, this, &FirmwareManager::onFlashFinished);
    connect(m_flasher, &StLinkFlasher::deviceConnected, this, &FirmwareManager::onDeviceConnected);
    connect(m_flasher, &StLinkFlasher::operationStarted, this, &FirmwareManager::onOperationStarted);
    connect(m_flasher, &StLinkFlasher::deviceUIDAvailable, this, &FirmwareManager::onDeviceUIDAvailable);
    connect(m_flasher, &StLinkFlasher::deviceUIDError, this, &FirmwareManager::onDeviceUIDError);

    // Authenticator for remote flow
    m_auth = auth;
    connect(m_auth, &Authenticator::requestStarted, this, &FirmwareManager::onAuthStarted);
    connect(m_auth, &Authenticator::requestFinished, this, &FirmwareManager::onAuthFinished);
    connect(m_auth, &Authenticator::authenticationFailed, this, &FirmwareManager::onAuthFailed);
    connect(m_auth, &Authenticator::authenticationSucceeded, this, &FirmwareManager::onAuthSucceeded);

    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &FirmwareManager::onFirmwareDownloadFinished);
}

FirmwareManager::~FirmwareManager()
{
    if (m_flasher)
    {
        // Invoke cleanup in the flasher's thread
        QMetaObject::invokeMethod(m_flasher, "stopAndCleanup");
    }
    else
    {
        m_flasherThread->quit();
    }

    // Wait for the thread to finish with timeout
    if (!m_flasherThread->wait(5000))
    {
        m_flasherThread->terminate();
        m_flasherThread->wait();
    }

    delete m_flasher;
    m_flasher = nullptr;
    // m_auth and m_networkManager are children, deleted automatically
}

void FirmwareManager::startUpdateProcess()
{
    if (m_flashInProgress)
    {
        emit statusMessage("Operation already in progress", Graphics::palette().error, MsgError);
        return;
    }

    m_currentOperation = OpFlash;
    m_pendingManifest = {};
    m_lastReadUidHex.clear();
    m_lastReadMcu.clear();
    emit operationStarted();
    emit statusMessage("Connecting to ST-Link...", Graphics::palette().textAll, MsgInfo);
    emit progressChanged(0, 100);

    // Trigger connection in the thread
    QMetaObject::invokeMethod(m_flasher, "connectDevice");
}

void FirmwareManager::startMassErase()
{
    if (m_flashInProgress)
    {
        emit statusMessage("Operation already in progress", Graphics::palette().error, MsgError);
        return;
    }

    m_currentOperation = OpErase;
    m_pendingManifest = {};
    m_lastReadUidHex.clear();
    m_lastReadMcu.clear();
    emit operationStarted();
    emit statusMessage("Connecting to ST-Link...", Graphics::palette().textAll, MsgInfo);
    emit progressChanged(0, 0);

    // Trigger connection in the thread
    QMetaObject::invokeMethod(m_flasher, "connectDevice");
}

bool FirmwareManager::isFlashInProgress() const
{
    return m_flashInProgress;
}

void FirmwareManager::onDeviceConnected(const QString &info)
{
    // emit statusMessage("Reading MCU ID...", Graphics::palette().textAll, MsgInfo);
    if (m_currentOperation == OpErase) {
        QMetaObject::invokeMethod(m_flasher, "performMassErase");
    } else {
        QMetaObject::invokeMethod(m_flasher, "readDeviceUID");
    }
}

void FirmwareManager::onFlashProgress(int value, int total)
{
    emit progressChanged(value, total);
}

void FirmwareManager::onFlashLog(const QString &msg)
{
    // Only update if operation is in progress to prevent overwriting final result/error
    emit statusMessage(msg, Graphics::palette().textAll, MsgInfo);
}

void FirmwareManager::onFlashFinished(bool success, const QString &msg)
{
    const OperationType finishedOperation = m_currentOperation;

    emit statusMessage(msg, success ? Graphics::palette().running : Graphics::palette().error, success ? MsgSuccess : MsgError);

    if (success)
    {
        emit progressChanged(100, 100);
        if (finishedOperation == OpFlash)
        {
            emit firmwareFlashed();
        }
    }

    m_flashInProgress = false;
    m_currentOperation = OpNone;
    emit operationFinished(success);

    // Disconnect after operation
    QMetaObject::invokeMethod(m_flasher, "disconnectDevice");
}

void FirmwareManager::onOperationStarted(const QString &operation)
{
    Q_UNUSED(operation);
    m_flashInProgress = true;
    emit operationStarted();
}

void FirmwareManager::onDeviceUIDAvailable(const QString &uidHex, const QString &mcu)
{
    const QString uidHexUpper = uidHex.toUpper();
    const QString normalizedMcu = mcu.trimmed().toUpper();
    m_lastReadUidHex = uidHexUpper;
    m_lastReadMcu = normalizedMcu;
    m_pendingManifest = {};

    requestLatestFirmwareManifest(uidHexUpper, normalizedMcu);
}

void FirmwareManager::onDeviceUIDError(const QString &message)
{
    failOperation("Failed to read MCU ID: " + message);
}

void FirmwareManager::onAuthStarted()
{
    // Keep UI busy; already disabled by onOperationStarted
}

void FirmwareManager::onAuthFinished()
{
    // No-op; wait for success/fail signals
}

void FirmwareManager::onAuthFailed(const QString &code, const QString &uiMessage)
{
    Q_UNUSED(code);
    failOperation(uiMessage);
}

void FirmwareManager::onAuthSucceeded(const QDateTime &validity, const QByteArray &token)
{
    Q_UNUSED(validity);
    Q_UNUSED(token);

    emit statusMessage("Remote auth OK", Graphics::palette().running, MsgSuccess);

    // End operation cleanly for now
    m_flashInProgress = false;
    emit operationFinished(true);
    QMetaObject::invokeMethod(m_flasher, "disconnectDevice");
}

void FirmwareManager::onFirmwareDownloadFinished(QNetworkReply *reply)
{
    const QString requestKind = reply->property(kRequestKindProperty).toString();
    if (requestKind == QLatin1String(kManifestRequestKind))
    {
        const QString requestedMcu = reply->property(kMcuProperty).toString();
        const QString requestedUid = reply->property(kUidHexProperty).toString();
        const QString localBinPath = cachedFirmwarePath(requestedUid);
        FirmwareCompatibility::ReleaseManifest cachedManifest;
        QString cachedManifestError;
        const bool hasCompatibleCachedFirmware = QFile::exists(localBinPath)
            && loadCachedFirmwareManifest(requestedUid, &cachedManifest)
            && cachedManifest.targetMcu.compare(requestedMcu, Qt::CaseInsensitive) == 0
            && isManifestCompatible(cachedManifest, &cachedManifestError);

        if (!hasCompatibleCachedFirmware && QFile::exists(localBinPath))
        {
            if (cachedManifestError.isEmpty())
            {
                emit logMessage(QStringLiteral("Ignoring cached firmware without valid metadata sidecar."));
            }
            else
            {
                emit logMessage(QStringLiteral("Ignoring cached firmware: %1").arg(cachedManifestError));
            }
        }

        if (reply->error() != QNetworkReply::NoError)
        {
            // Server error responses carry the actual reason as plain text/JSON body,
            // which reply->errorString() does not include - prefer that when present.
            const QByteArray errorBody = reply->readAll();
            const QString jsonError = extractJsonErrorMessage(errorBody);
            const QString bodyText = QString::fromUtf8(errorBody).trimmed();
            const QString errorText = !jsonError.isEmpty() ? jsonError
                : (!bodyText.isEmpty() ? bodyText : reply->errorString());
            reply->deleteLater();

            if (hasCompatibleCachedFirmware)
            {
                emit statusMessage("Failed to read latest firmware metadata. Flashing compatible local firmware...",
                                   Graphics::palette().running,
                                   MsgInfo);
                QMetaObject::invokeMethod(m_flasher, "flashFirmware", Q_ARG(QString, localBinPath));
                return;
            }

            failOperation(QStringLiteral("Failed to read latest firmware metadata: %1").arg(errorText));
            return;
        }

        const QByteArray data = reply->readAll();
        reply->deleteLater();
        if (data.isEmpty())
        {
            failOperation("Latest firmware metadata is empty.");
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(data);
        if (!document.isObject())
        {
            failOperation("Latest firmware metadata is invalid.");
            return;
        }

        const FirmwareCompatibility::ReleaseManifest manifest =
            FirmwareCompatibility::releaseManifestFromJson(document.object());
        if (!manifest.isValid)
        {
            failOperation("Latest firmware metadata is incomplete.");
            return;
        }

        if (manifest.targetMcu.compare(requestedMcu, Qt::CaseInsensitive) != 0)
        {
            failOperation("Latest firmware metadata does not match the connected MCU.");
            return;
        }

        QString manifestError;
        if (!isManifestCompatible(manifest, &manifestError))
        {
            failOperation(manifestError);
            return;
        }

        const QString email = CustomSettings::getUserEmail();
        if (email.isEmpty() || !CustomSettings::hasValidLogin())
        {
            failOperation("Unknown user or invalid session. Please log in.", MsgLoginRequired);
            return;
        }

        m_pendingManifest = manifest;

        if (hasCompatibleCachedFirmware)
        {
            const int versionComparison = compareFirmwareVersions(cachedManifest.firmwareVersion, manifest.firmwareVersion);
            const bool isSameRelease = cachedManifest.firmwareVersion == manifest.firmwareVersion
                && cachedManifest.releaseId == manifest.releaseId
                && cachedManifest.protocolVersionText == manifest.protocolVersionText
                && cachedManifest.minDesktopVersion == manifest.minDesktopVersion;

            if (isSameRelease || versionComparison > 0)
            {
                const QString statusText = isSameRelease
                    ? QStringLiteral("Latest firmware already cached. Flashing...")
                    : QStringLiteral("Cached firmware is up to date. Flashing local copy...");
                emit statusMessage(statusText, Graphics::palette().running, MsgInfo);
                QMetaObject::invokeMethod(m_flasher, "flashFirmware", Q_ARG(QString, localBinPath));
                return;
            }
        }

        requestFirmwareBinary(requestedUid, requestedMcu);
        return;
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        const QString errorText = reply->errorString();
        reply->deleteLater();
        failOperation(QStringLiteral("Download error: %1").arg(errorText));
        return;
    }

    const QString expectedName = m_lastReadUidHex + QStringLiteral(".bin");
    const QString filename = extractReplyFilename(reply);
    if (filename.compare(expectedName, Qt::CaseInsensitive) != 0)
    {
        reply->deleteLater();
        failOperation(QStringLiteral("Error: Downloaded file (%1) does not match MCU UID.").arg(filename));
        return;
    }

    QString headerError;
    if (!validateFirmwareReplyHeaders(reply, reply->property(kMcuProperty).toString(), &headerError))
    {
        reply->deleteLater();
        failOperation(headerError);
        return;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (data.isEmpty())
    {
        failOperation("Error: Empty response from server.");
        return;
    }

    const QString serverError = extractJsonErrorMessage(data);
    if (!serverError.isEmpty())
    {
        failOperation("Server reply: " + serverError);
        return;
    }

    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    if (!dir.exists() && !dir.mkpath(dir.path()))
    {
        failOperation("Error: Could not create firmware cache directory.");
        return;
    }

    const QString filePath = cachedFirmwarePath(m_lastReadUidHex);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        failOperation("Error: Could not save firmware file: " + filename);
        return;
    }
    file.write(data);
    file.close();

    if (!saveCachedFirmwareManifest(m_lastReadUidHex, m_pendingManifest))
    {
        emit logMessage(QStringLiteral("Failed to write firmware metadata sidecar for %1.").arg(filename));
    }

    emit statusMessage("Download complete. Flashing...", Graphics::palette().textAll, MsgInfo);
    QMetaObject::invokeMethod(m_flasher, "flashFirmware", Q_ARG(QString, filePath));
}

QString FirmwareManager::cachedFirmwarePath(const QString &uidHex) const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/") + uidHex + QStringLiteral(".bin");
}

QString FirmwareManager::cachedFirmwareMetadataPath(const QString &uidHex) const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/") + uidHex + QStringLiteral(".manifest.json");
}

bool FirmwareManager::loadCachedFirmwareManifest(const QString &uidHex, FirmwareCompatibility::ReleaseManifest *manifest) const
{
    QFile file(cachedFirmwareMetadataPath(uidHex));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
    {
        return false;
    }

    const FirmwareCompatibility::ReleaseManifest cachedManifest =
        FirmwareCompatibility::releaseManifestFromJson(document.object());
    if (!cachedManifest.isValid)
    {
        return false;
    }

    if (manifest != nullptr)
    {
        *manifest = cachedManifest;
    }

    return true;
}

bool FirmwareManager::saveCachedFirmwareManifest(const QString &uidHex, const FirmwareCompatibility::ReleaseManifest &manifest) const
{
    if (!manifest.isValid)
    {
        return false;
    }

    QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    if (!dir.exists() && !dir.mkpath(dir.path()))
    {
        return false;
    }

    QJsonObject object;
    object.insert(QStringLiteral("schema_version"), 1);
    object.insert(QStringLiteral("target_mcu"), manifest.targetMcu);
    object.insert(QStringLiteral("firmware_version"), manifest.firmwareVersion);
    object.insert(QStringLiteral("release_id"), manifest.releaseId);
    object.insert(QStringLiteral("protocol_version"), manifest.protocolVersionText);
    object.insert(QStringLiteral("min_desktop_version"), manifest.minDesktopVersion);
    object.insert(QStringLiteral("published_at"), manifest.publishedAt);
    object.insert(QStringLiteral("channel"), manifest.channel);
    object.insert(QStringLiteral("binary_name"), manifest.binaryName);

    QFile file(cachedFirmwareMetadataPath(uidHex));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }

    const qint64 written = file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return written >= 0;
}

bool FirmwareManager::isManifestCompatible(const FirmwareCompatibility::ReleaseManifest &manifest, QString *message) const
{
    QString errorMessage;
    if (!manifest.isValid)
    {
        errorMessage = QStringLiteral("Firmware metadata is invalid.");
    }
    else if (!FirmwareCompatibility::isDesktopVersionCompatible(manifest.minDesktopVersion))
    {
        errorMessage = QStringLiteral("Latest firmware requires Sniffy %1 or newer.")
                           .arg(manifest.minDesktopVersion);
    }
    else if (!FirmwareCompatibility::isSupportedProtocolVersion(manifest.protocolVersion))
    {
        errorMessage = QStringLiteral("Latest firmware protocol version %1 is not supported by this Sniffy release.")
                           .arg(manifest.protocolVersionText);
    }

    if (message != nullptr)
    {
        *message = errorMessage;
    }

    return errorMessage.isEmpty();
}

bool FirmwareManager::validateFirmwareReplyHeaders(QNetworkReply *reply, const QString &expectedMcu, QString *message) const
{
    const QString targetMcu = QString::fromUtf8(reply->rawHeader("X-Sniffy-Target-MCU")).trimmed();
    const QString firmwareVersion = QString::fromUtf8(reply->rawHeader("X-Sniffy-Firmware-Version")).trimmed();
    const QString protocolVersion = QString::fromUtf8(reply->rawHeader("X-Sniffy-Protocol-Version")).trimmed();
    const QString minimumDesktopVersion = QString::fromUtf8(reply->rawHeader("X-Sniffy-Min-Desktop-Version")).trimmed();
    const QString releaseId = QString::fromUtf8(reply->rawHeader("X-Sniffy-Release-Id")).trimmed();

    QString errorMessage;
    if (!m_pendingManifest.isValid)
    {
        errorMessage = QStringLiteral("Firmware response is missing validated manifest context.");
    }
    else if (targetMcu.isEmpty() || firmwareVersion.isEmpty() || protocolVersion.isEmpty()
             || minimumDesktopVersion.isEmpty() || releaseId.isEmpty())
    {
        errorMessage = QStringLiteral("Firmware response is missing compatibility headers.");
    }
    else if (targetMcu.compare(expectedMcu, Qt::CaseInsensitive) != 0)
    {
        errorMessage = QStringLiteral("Firmware response does not match the connected MCU.");
    }
    else if (targetMcu.compare(m_pendingManifest.targetMcu, Qt::CaseInsensitive) != 0
             || firmwareVersion != m_pendingManifest.firmwareVersion
             || protocolVersion != m_pendingManifest.protocolVersionText
             || minimumDesktopVersion != m_pendingManifest.minDesktopVersion
             || releaseId != m_pendingManifest.releaseId)
    {
        errorMessage = QStringLiteral("Firmware response metadata does not match the validated manifest.");
    }

    if (message != nullptr)
    {
        *message = errorMessage;
    }

    return errorMessage.isEmpty();
}

void FirmwareManager::requestLatestFirmwareManifest(const QString &uidHex, const QString &mcu)
{
    emit statusMessage("Checking latest firmware compatibility...", Graphics::palette().running, MsgInfo);

    QUrl url(QStringLiteral("https://sniffylab.com/scripts/sniffy_fw_latest.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("mcu"), mcu);
    url.setQuery(query);

    QNetworkRequest request(url);
    QNetworkReply *reply = m_networkManager->get(request);
    reply->setProperty(kRequestKindProperty, QString::fromLatin1(kManifestRequestKind));
    reply->setProperty(kUidHexProperty, uidHex);
    reply->setProperty(kMcuProperty, mcu);
}

void FirmwareManager::requestFirmwareBinary(const QString &uidHex, const QString &mcu)
{
    QString email = CustomSettings::getUserEmail();
    QString sessionId = QString::number(CustomSettings::getTokenValidity().toSecsSinceEpoch());

    emit statusMessage("Requesting remote firmware...", Graphics::palette().running, MsgInfo);

    QUrl url(QStringLiteral("https://sniffylab.com/scripts/sniffy_bin_req.php"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("email"), email);
    query.addQueryItem(QStringLiteral("session_ID"), sessionId);
    query.addQueryItem(QStringLiteral("MCU_UID"), uidHex);
    query.addQueryItem(QStringLiteral("MCU"), mcu);
    url.setQuery(query);

    QNetworkRequest request(url);
    QNetworkReply *reply = m_networkManager->get(request);
    reply->setProperty(kRequestKindProperty, QString::fromLatin1(kBinaryRequestKind));
    reply->setProperty(kUidHexProperty, uidHex);
    reply->setProperty(kMcuProperty, mcu);

    qDebug() << "[FirmwareManager] Downloading firmware from:" << url.toString();
}

void FirmwareManager::failOperation(const QString &msg, int msgType)
{
    emit statusMessage(msg, Graphics::palette().error, msgType);
    m_flashInProgress = false;
    emit operationFinished(false);
    QMetaObject::invokeMethod(m_flasher, "disconnectDevice");
}
