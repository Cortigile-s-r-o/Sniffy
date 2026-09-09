#include "appupdatemanager.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QVersionNumber>

#include "firmwarecompatibility.h"

namespace {

constexpr auto kMetadataRequestKind = "metadata";
constexpr auto kDownloadRequestKind = "assetDownload";
constexpr auto kUpdateEndpoint = "https://sniffylab.com/scripts/sniffy_desktop_latest.php";

QString safeFileStem(const QString &value)
{
    QString stem = value;
    stem.replace(' ', '_');

    QString sanitized;
    sanitized.reserve(stem.size());
    for (const QChar ch : stem) {
        if (ch.isLetterOrNumber() || ch == '.' || ch == '_' || ch == '-') {
            sanitized.append(ch);
        }
    }

    return sanitized.isEmpty() ? QStringLiteral("sniffy-update") : sanitized;
}

} // namespace

AppUpdateManager::AppUpdateManager(QObject *parent)
    : QObject(parent),
      m_networkManager(new QNetworkAccessManager(this))
{
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &AppUpdateManager::onNetworkFinished);
}

AppUpdateManager::~AppUpdateManager()
{
    clearDownloadState();
}

QString AppUpdateManager::currentVersion() const
{
    return FirmwareCompatibility::applicationVersionText();
}

bool AppUpdateManager::hasAvailableUpdate() const
{
    return m_updateAvailable;
}

QString AppUpdateManager::availableVersion() const
{
    return m_availableRelease.version;
}

void AppUpdateManager::checkForUpdates(bool manual)
{
    m_manualCheckPending = m_manualCheckPending || manual;

    QUrl url(QString::fromLatin1(kUpdateEndpoint));
    QUrlQuery query;
    const QString platform = currentPlatform();
    if (!platform.isEmpty()) {
        query.addQueryItem(QStringLiteral("platform"), platform);
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Sniffy/%1").arg(currentVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_networkManager->get(request);
    reply->setProperty("sniffyRequestKind", QString::fromLatin1(kMetadataRequestKind));

    if (manual) {
        emit updateStatusTextChanged(QStringLiteral("Checking for updates..."));
    }
}

void AppUpdateManager::installAvailableUpdate()
{
    if (!m_updateAvailable || !m_availableRelease.isValid) {
        emit popupMessageRequested(QStringLiteral("No newer desktop update is available."));
        return;
    }

    if (m_installInProgress) {
        emit popupMessageRequested(QStringLiteral("Update download is already in progress."));
        return;
    }

    const QString sourceUrl = !m_availableRelease.preferredAssetDownloadUrl.isEmpty()
        ? m_availableRelease.preferredAssetDownloadUrl
        : m_availableRelease.preferredAssetDirectUrl;
    if (sourceUrl.isEmpty()) {
        emit popupMessageRequested(QStringLiteral("The selected update does not expose a downloadable asset."));
        return;
    }

    m_downloadTargetPath = targetDownloadPath(m_availableRelease);
    if (m_downloadTargetPath.isEmpty()) {
        emit popupMessageRequested(QStringLiteral("Unable to resolve the local update file path."));
        return;
    }

    clearDownloadState();
    m_downloadFile = new QSaveFile(m_downloadTargetPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        delete m_downloadFile;
        m_downloadFile = nullptr;
        emit popupMessageRequested(QStringLiteral("Unable to open the local update file for writing."));
        return;
    }

    QNetworkRequest request{QUrl(sourceUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Sniffy/%1").arg(currentVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_downloadReply = m_networkManager->get(request);
    m_downloadReply->setProperty("sniffyRequestKind", QString::fromLatin1(kDownloadRequestKind));
    connect(m_downloadReply, &QIODevice::readyRead, this, &AppUpdateManager::onDownloadReadyRead);
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, &AppUpdateManager::onDownloadProgress);

    m_installInProgress = true;
    emit bannerActionStateChanged(QStringLiteral("Downloading..."), false);
    emit updateStatusTextChanged(QStringLiteral("Downloading update %1...").arg(m_availableRelease.version));
}

void AppUpdateManager::onNetworkFinished(QNetworkReply *reply)
{
    const QString requestKind = reply->property("sniffyRequestKind").toString();
    if (requestKind == QLatin1String(kMetadataRequestKind)) {
        const QByteArray payload = reply->readAll();
        handleMetadataReply(reply, payload);
        reply->deleteLater();
        return;
    }

    if (requestKind == QLatin1String(kDownloadRequestKind)) {
        handleDownloadReply(reply);
        reply->deleteLater();
        return;
    }

    reply->deleteLater();
}

void AppUpdateManager::onDownloadReadyRead()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (reply == nullptr || reply != m_downloadReply) {
        return;
    }

    writeDownloadChunk(reply);
}

void AppUpdateManager::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (!m_installInProgress || bytesReceived <= 0 || bytesTotal <= 0) {
        return;
    }

    const qint64 percent = (bytesReceived * 100) / bytesTotal;
    emit updateStatusTextChanged(QStringLiteral("Downloading update %1... %2%").arg(m_availableRelease.version).arg(percent));
}

QString AppUpdateManager::currentPlatform() const
{
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QString();
#endif
}

int AppUpdateManager::compareVersions(const QString &leftVersion, const QString &rightVersion) const
{
    const QVersionNumber left = QVersionNumber::fromString(leftVersion.trimmed());
    const QVersionNumber right = QVersionNumber::fromString(rightVersion.trimmed());
    if (!left.isNull() && !right.isNull()) {
        return QVersionNumber::compare(left, right);
    }

    return QString::compare(leftVersion.trimmed(), rightVersion.trimmed(), Qt::CaseInsensitive);
}

AppUpdateManager::ReleaseInfo AppUpdateManager::parseLatestRelease(const QByteArray &payload, QString *errorMessage) const
{
    ReleaseInfo info;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("The update server returned invalid JSON.");
        }
        return info;
    }

    const QJsonObject root = document.object();
    info.version = root.value(QStringLiteral("version")).toString().trimmed();
    info.releaseTag = root.value(QStringLiteral("release_tag")).toString().trimmed();
    info.releasePage = root.value(QStringLiteral("release_page")).toString().trimmed();
    info.publishedAt = root.value(QStringLiteral("published_at")).toString().trimmed();
    info.preferredAssetKey = root.value(QStringLiteral("preferred_asset_key")).toString().trimmed();

    QJsonObject assetObject = root.value(QStringLiteral("preferred_asset")).toObject();
    if (assetObject.isEmpty()) {
        const QJsonObject assets = root.value(QStringLiteral("assets")).toObject();
        if (!info.preferredAssetKey.isEmpty()) {
            assetObject = assets.value(info.preferredAssetKey).toObject();
        }
        if (assetObject.isEmpty() && !assets.isEmpty()) {
            const auto firstAsset = assets.begin();
            if (firstAsset != assets.end() && firstAsset->isObject()) {
                info.preferredAssetKey = firstAsset.key();
                assetObject = firstAsset->toObject();
            }
        }
    }

    info.preferredAssetLabel = assetObject.value(QStringLiteral("label")).toString().trimmed();
    info.preferredAssetName = assetObject.value(QStringLiteral("name")).toString().trimmed();
    info.preferredAssetDirectUrl = assetObject.value(QStringLiteral("direct_url")).toString().trimmed();
    info.preferredAssetDownloadUrl = assetObject.value(QStringLiteral("download_url")).toString().trimmed();

    info.isValid = !info.version.isEmpty()
        && !info.releaseTag.isEmpty()
        && (!info.preferredAssetDirectUrl.isEmpty() || !info.preferredAssetDownloadUrl.isEmpty());

    if (!info.isValid && errorMessage != nullptr) {
        *errorMessage = QStringLiteral("The update server response is missing required release fields.");
    }

    return info;
}

void AppUpdateManager::handleMetadataReply(QNetworkReply *reply, const QByteArray &payload)
{
    const bool manual = m_manualCheckPending;
    m_manualCheckPending = false;

    if (reply->error() != QNetworkReply::NoError) {
        emit updateStatusTextChanged(QStringLiteral("Unable to verify the latest desktop version."));
        if (manual) {
            emit popupMessageRequested(QStringLiteral("Update check failed: %1").arg(reply->errorString()));
        }
        return;
    }

    QString parseErrorMessage;
    const ReleaseInfo latestRelease = parseLatestRelease(payload, &parseErrorMessage);
    if (!latestRelease.isValid) {
        emit updateStatusTextChanged(QStringLiteral("Unable to verify the latest desktop version."));
        if (manual) {
            emit popupMessageRequested(parseErrorMessage.isEmpty() ? QStringLiteral("Update check failed.") : parseErrorMessage);
        }
        return;
    }

    const int compareResult = compareVersions(latestRelease.version, currentVersion());
    if (compareResult > 0) {
        m_availableRelease = latestRelease;
        m_updateAvailable = true;
        emit updateAvailabilityChanged(true, latestRelease.version);
        emit bannerActionStateChanged(QStringLiteral("Install and relaunch"), true);
        emit updateStatusTextChanged(QStringLiteral("Update available: %1").arg(latestRelease.version));
        if (manual) {
            emit popupMessageRequested(QStringLiteral("A newer desktop version is available: %1").arg(latestRelease.version));
        }
        return;
    }

    m_availableRelease = ReleaseInfo();
    if (m_updateAvailable) {
        m_updateAvailable = false;
        emit updateAvailabilityChanged(false, QString());
    }
    emit bannerActionStateChanged(QStringLiteral("Install and relaunch"), true);
    emit updateStatusTextChanged(QStringLiteral("Up to date (%1)").arg(currentVersion()));
    if (manual) {
        emit popupMessageRequested(QStringLiteral("You are using the latest desktop version."));
    }
}

void AppUpdateManager::handleDownloadReply(QNetworkReply *reply)
{
    if (reply != m_downloadReply) {
        return;
    }

    QString writeErrorMessage;
    if (!writeDownloadChunk(reply, &writeErrorMessage)) {
        failInstall(writeErrorMessage);
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        failInstall(QStringLiteral("Update download failed: %1").arg(reply->errorString()));
        return;
    }

    if (m_downloadFile == nullptr) {
        failInstall(QStringLiteral("The local update file is no longer available."));
        return;
    }

    if (!m_downloadFile->commit()) {
        failInstall(QStringLiteral("Unable to finalize the downloaded update package."));
        return;
    }

    delete m_downloadFile;
    m_downloadFile = nullptr;

    QString errorMessage;
    QString launchMessage;
    if (!prepareInstallerHandoff(m_downloadTargetPath, &errorMessage, &launchMessage)) {
        failInstall(errorMessage);
        return;
    }

    clearDownloadState();
    emit updateStatusTextChanged(QStringLiteral("Update package ready: %1").arg(m_availableRelease.version));
    emit popupMessageRequested(launchMessage);
    emit quitRequested();
}

void AppUpdateManager::failInstall(const QString &message)
{
    clearDownloadState();
    emit bannerActionStateChanged(QStringLiteral("Install and relaunch"), true);
    emit updateStatusTextChanged(QStringLiteral("Update available: %1").arg(m_availableRelease.version));
    emit popupMessageRequested(message);
}

void AppUpdateManager::clearDownloadState()
{
    if (m_downloadReply != nullptr) {
        disconnect(m_downloadReply, nullptr, this, nullptr);
        if (m_downloadReply->isRunning()) {
            m_downloadReply->abort();
        }
        m_downloadReply = nullptr;
    }

    if (m_downloadFile != nullptr) {
        m_downloadFile->cancelWriting();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }

    m_installInProgress = false;
}

QString AppUpdateManager::updateDownloadDirectory() const
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (baseDir.isEmpty()) {
        baseDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    if (baseDir.isEmpty()) {
        return QString();
    }

    const QString path = QDir(baseDir).filePath(QStringLiteral("updates"));
    if (!QDir().mkpath(path)) {
        return QString();
    }

    return path;
}

QString AppUpdateManager::targetDownloadPath(const ReleaseInfo &release) const
{
    const QString directory = updateDownloadDirectory();
    if (directory.isEmpty()) {
        return QString();
    }

    const QString assetName = !release.preferredAssetName.isEmpty()
        ? release.preferredAssetName
        : QStringLiteral("Sniffy-%1").arg(release.version);
    return QDir(directory).filePath(safeFileStem(assetName));
}

QString AppUpdateManager::installedExecutablePath() const
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

QString AppUpdateManager::installedRootPath() const
{
    const QFileInfo appInfo(QCoreApplication::applicationFilePath());
    QDir appDir = appInfo.dir();
    if (appDir.dirName().compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0 && appDir.cdUp()) {
        return QDir::toNativeSeparators(appDir.absolutePath());
    }

    return QDir::toNativeSeparators(appInfo.absolutePath());
}

bool AppUpdateManager::writeDownloadChunk(QNetworkReply *reply, QString *errorMessage)
{
    if (reply == nullptr || reply != m_downloadReply || m_downloadFile == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("The local update file is no longer available.");
        }
        return false;
    }

    const QByteArray chunk = reply->readAll();
    if (!chunk.isEmpty() && m_downloadFile->write(chunk) != chunk.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to write the downloaded update package to disk.");
        }
        return false;
    }

    return true;
}

bool AppUpdateManager::prepareInstallerHandoff(const QString &installerPath, QString *errorMessage, QString *launchMessage)
{
    if (!QFileInfo::exists(installerPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("The downloaded update package is missing on disk.");
        }
        return false;
    }

#ifdef Q_OS_WIN
    const QString scriptPath = createWindowsInstallerScript(installerPath, errorMessage);
    if (scriptPath.isEmpty()) {
        return false;
    }

    const bool started = QProcess::startDetached(
        QStringLiteral("powershell.exe"),
        {
            QStringLiteral("-NoProfile"),
            QStringLiteral("-ExecutionPolicy"),
            QStringLiteral("Bypass"),
            QStringLiteral("-File"),
            QDir::toNativeSeparators(scriptPath)
        }
    );
    if (!started) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to launch the Windows installer helper process.");
        }
        return false;
    }

    if (launchMessage != nullptr) {
        *launchMessage = QStringLiteral("The silent update will start after Sniffy closes and the app will relaunch when installation finishes.");
    }
    return true;
#elif defined(Q_OS_LINUX)
    const QString scriptPath = createLinuxInstallerScript(installerPath, errorMessage);
    if (scriptPath.isEmpty()) {
        return false;
    }

    const bool started = QProcess::startDetached(QStringLiteral("/bin/sh"), {scriptPath});
    if (!started) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to launch the Linux installer helper process.");
        }
        return false;
    }

    if (launchMessage != nullptr) {
        *launchMessage = QStringLiteral("The update will continue after Sniffy closes. Linux may request administrator authorization to install the package.");
    }
    return true;
#else
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(installerPath));
    if (!opened) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to open the downloaded update package.");
        }
        return false;
    }
    if (launchMessage != nullptr) {
        *launchMessage = QStringLiteral("The downloaded update package was opened.");
    }
    return true;
#endif
}

QString AppUpdateManager::createWindowsInstallerScript(const QString &installerPath, QString *errorMessage) const
{
    const QString directory = updateDownloadDirectory();
    if (directory.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to prepare the Windows installer helper directory.");
        }
        return QString();
    }

    const QString scriptPath = QDir(directory).filePath(QStringLiteral("install-update-%1.ps1").arg(QCoreApplication::applicationPid()));
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to write the Windows installer helper script.");
        }
        return QString();
    }

    const QString appPath = installedExecutablePath();
    const QString installRoot = installedRootPath();
    const QString body = QStringLiteral(
        "$installer = '%1'\n"
        "$appPath = '%2'\n"
        "$installRoot = '%3'\n"
        "$pidToWait = %4\n"
        "while (Get-Process -Id $pidToWait -ErrorAction SilentlyContinue) {\n"
        "    Start-Sleep -Milliseconds 500\n"
        "}\n"
        "$installerLower = $installer.ToLowerInvariant()\n"
        "try {\n"
        "    if ($installerLower.EndsWith('.msi')) {\n"
        "        Start-Process -FilePath 'msiexec.exe' -ArgumentList @('/i', $installer, '/qn', '/norestart', \"INSTALLDIR=$installRoot\") -Wait | Out-Null\n"
        "    } else {\n"
        "        Start-Process -FilePath $installer -ArgumentList @('/S', \"/D=$installRoot\") -Wait | Out-Null\n"
        "    }\n"
        "} finally {\n"
        "    if (Test-Path -LiteralPath $appPath) {\n"
        "        Start-Process -FilePath $appPath | Out-Null\n"
        "    }\n"
        "    Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue\n"
        "}\n")
        .arg(
            quoteForPowerShell(QDir::toNativeSeparators(installerPath)),
            quoteForPowerShell(appPath),
            quoteForPowerShell(installRoot),
            QString::number(QCoreApplication::applicationPid())
        );
    script.write(body.toUtf8());
    script.close();
    return scriptPath;
}

QString AppUpdateManager::createLinuxInstallerScript(const QString &installerPath, QString *errorMessage) const
{
    const QString directory = updateDownloadDirectory();
    if (directory.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to prepare the Linux installer helper directory.");
        }
        return QString();
    }

    const QString scriptPath = QDir(directory).filePath(QStringLiteral("install-update-%1.sh").arg(QCoreApplication::applicationPid()));
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to write the Linux installer helper script.");
        }
        return QString();
    }

    const QString appPath = installedExecutablePath();
    const QString body = QStringLiteral(
        "#!/bin/sh\n"
        "installer=%1\n"
        "app_path=%2\n"
        "pid_to_wait=%3\n"
        "while kill -0 \"$pid_to_wait\" 2>/dev/null; do\n"
        "  sleep 1\n"
        "done\n"
        "status=1\n"
        "if command -v pkexec >/dev/null 2>&1 && command -v dpkg >/dev/null 2>&1; then\n"
        "  pkexec dpkg -i \"$installer\"\n"
        "  status=$?\n"
        "elif [ \"$(id -u)\" -eq 0 ] && command -v dpkg >/dev/null 2>&1; then\n"
        "  dpkg -i \"$installer\"\n"
        "  status=$?\n"
        "else\n"
        "  xdg-open \"$installer\" >/dev/null 2>&1 &\n"
        "  status=0\n"
        "fi\n"
        "if [ \"$status\" -eq 0 ] && [ -x \"$app_path\" ]; then\n"
        "  nohup \"$app_path\" >/dev/null 2>&1 &\n"
        "fi\n"
        "rm -- \"$0\"\n")
        .arg(
            quoteForShell(installerPath),
            quoteForShell(appPath),
            QString::number(QCoreApplication::applicationPid())
        );
    script.write(body.toUtf8());
    script.close();
    script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return scriptPath;
}

QString AppUpdateManager::quoteForPowerShell(const QString &value) const
{
    QString escaped = value;
    escaped.replace("'", "''");
    return escaped;
}

QString AppUpdateManager::quoteForShell(const QString &value) const
{
    QString escaped = value;
    escaped.replace("'", "'\"'\"'");
    return QStringLiteral("'%1'").arg(escaped);
}