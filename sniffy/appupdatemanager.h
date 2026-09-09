#ifndef APPUPDATEMANAGER_H
#define APPUPDATEMANAGER_H

#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

class AppUpdateManager : public QObject
{
    Q_OBJECT

public:
    struct ReleaseInfo {
        bool isValid = false;
        QString version;
        QString releaseTag;
        QString releasePage;
        QString publishedAt;
        QString preferredAssetKey;
        QString preferredAssetLabel;
        QString preferredAssetName;
        QString preferredAssetDirectUrl;
        QString preferredAssetDownloadUrl;
    };

    explicit AppUpdateManager(QObject *parent = nullptr);
    ~AppUpdateManager();

    QString currentVersion() const;
    bool hasAvailableUpdate() const;
    QString availableVersion() const;

public slots:
    void checkForUpdates(bool manual = false);
    void installAvailableUpdate();

signals:
    void updateAvailabilityChanged(bool available, const QString &version);
    void updateActionStateChanged(const QString &text, bool enabled);
    void updateStatusTextChanged(const QString &text);
    void popupMessageRequested(const QString &text);
    void quitRequested();

private slots:
    void onNetworkFinished(QNetworkReply *reply);
    void onDownloadReadyRead();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);

private:
    QString currentPlatform() const;
    int compareVersions(const QString &leftVersion, const QString &rightVersion) const;
    ReleaseInfo parseLatestRelease(const QByteArray &payload, QString *errorMessage) const;
    void handleMetadataReply(QNetworkReply *reply, const QByteArray &payload);
    void handleDownloadReply(QNetworkReply *reply);
    void failInstall(const QString &message);
    void clearDownloadState();
    bool writeDownloadChunk(QNetworkReply *reply, QString *errorMessage = nullptr);
    QString updateDownloadDirectory() const;
    QString targetDownloadPath(const ReleaseInfo &release) const;
    QString installedExecutablePath() const;
    QString installedRootPath() const;
    bool prepareInstallerHandoff(const QString &installerPath, QString *errorMessage, QString *launchMessage);
    QString createWindowsInstallerScript(const QString &installerPath, QString *errorMessage) const;
    QString createLinuxInstallerScript(const QString &installerPath, QString *errorMessage) const;
    QString quoteForPowerShell(const QString &value) const;
    QString quoteForShell(const QString &value) const;

    QNetworkAccessManager *m_networkManager;
    QPointer<QNetworkReply> m_downloadReply;
    QSaveFile *m_downloadFile = nullptr;
    QString m_downloadTargetPath;
    ReleaseInfo m_availableRelease;
    bool m_updateAvailable = false;
    bool m_manualCheckPending = false;
    bool m_installInProgress = false;
};

#endif // APPUPDATEMANAGER_H