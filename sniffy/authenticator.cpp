#include "authenticator.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QSysInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QRegularExpression>

#include "customsettings.h"

Authenticator::Authenticator(QObject *parent)
    : QObject(parent)
{
    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, &QNetworkAccessManager::finished, this, &Authenticator::onFinished);
    timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(15000); // 15 seconds timeout for network requests
    connect(timeoutTimer, &QTimer::timeout, this, &Authenticator::onTimeout);
    
    pollTimer = new QTimer(this);
    pollTimer->setInterval(3000); // Poll after 3seconds used for login status checking
    connect(pollTimer, &QTimer::timeout, this, &Authenticator::checkAuthStatus);
    
    qDebug() << "[Auth] Authenticator initialized";
    authenticationSentManual = false;
}

void Authenticator::beginManualAttempt(const QString &email)
{
    // Start each manual login flow with a fresh session id to avoid stale browser context.
    pendingManualEmail = email.trimmed();
    currentSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    qDebug() << "[Auth] New manual auth session:" << currentSessionId << "Email:" << pendingManualEmail;
    emit sessionStarted(currentSessionId);
}

void Authenticator::setDemoMode(bool demo)
{
    connectedInDemoMode = demo;
}

void Authenticator::checkLogin(const QString &email)
{
    if (currentReply)
        return; // already running
    authenticationSentManual = true;
    beginManualAttempt(email);
    
    // Ensure polling is stopped for manual check
    if (pollTimer->isActive()) pollTimer->stop();
    
    qDebug() << "[Auth] Checking login status for" << pendingManualEmail << "Session:" << currentSessionId;
    emit requestStarted();
    startRequest(pendingManualEmail, currentDeviceName, currentMcuId);
}

void Authenticator::setConnectedDevice(const QString &name, const QString &id)
{
    qDebug() << "[Auth] Device info updated:" << name << id;
    currentDeviceName = name;
    currentMcuId = id;
}

void Authenticator::startPolling(bool immediate)
{
    qDebug() << "[Auth] Starting polling for session:" << currentSessionId << "Immediate:" << immediate;
    emit requestStarted();
    pollTimer->start();
    if (immediate) {
        checkAuthStatus(); // Check immediately
    }
}

void Authenticator::stopPolling()
{
    pollTimer->stop();
    // Do NOT clear currentSessionId - it persists for the app lifetime
    if (currentReply) {
        currentReply->abort();
        currentReply = nullptr;
    }
    authenticationSentManual = false;
    emit requestFinished();
    qDebug() << "[Auth] Polling stopped";
}

void Authenticator::tokenRefresh(const QString &deviceName, const QString &mcuId, bool isDemoMode)
{
    if(currentReply) return; // Request already in progress
    const QString email = CustomSettings::getUserEmail();
    if (email.isEmpty() || email == "Unknown user") return;
    authenticationSentManual = false;
    
    qDebug() << "[Auth] Token refresh requested for" << email << "Device:" << deviceName << "MCU_ID:" << mcuId << "Demo:" << isDemoMode;
    // For refresh, use the renewal flow with device info
    startRequest(email, deviceName, mcuId);
}

void Authenticator::startRequest(const QString &email, const QString &deviceName, const QString &mcuId)
{

    qDebug() << "[Auth] Starting" << "request for" << email 
                << "Device:" << deviceName << "MCU_ID:" << mcuId 
                << "Session:" << currentSessionId;
    
    // Use auth_check for renewal or polling, auth_start is only used from browser
    QUrl authUrl(QStringLiteral("https://sniffylab.com/scripts/sniffy_auth_check.php"));
    
    QUrlQuery query;
    query.addQueryItem("email", email);
    if (!deviceName.isEmpty()) query.addQueryItem("Device_name", deviceName);
    if (!mcuId.isEmpty()) query.addQueryItem("MCU_ID", mcuId);
    if (!currentSessionId.isEmpty()) query.addQueryItem("session_id", currentSessionId);
    
    QNetworkRequest req(authUrl);
    const QString userAgent = QString("sniffy/1.0 (%1; %2; %3; %4)")
            .arg(QSysInfo::machineHostName())
            .arg(QSysInfo::productType())
            .arg(QSysInfo::productVersion())
            .arg(QSysInfo::currentCpuArchitecture());
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    if(authenticationSentManual == true && currentSessionId.isEmpty()){
        // Only emit requestStarted if not polling (polling already emitted it)
        emit requestStarted();
    }

    QByteArray postData = query.toString(QUrl::FullyEncoded).toUtf8();
    qDebug() << "[Auth] Sending POST data to" << authUrl.toString() << ":" << postData;
    currentReply = networkManager->post(req, postData);
    if (timeoutTimer) timeoutTimer->start();
}

void Authenticator::onTimeout()
{
    if (currentReply) currentReply->abort();
}

void Authenticator::checkAuthStatus()
{
    // If polling is not active, stop (safety check)
    if (!pollTimer->isActive()) {
        return;
    }
    
    if (currentReply) return; // Previous request still in progress
    
    const QString email = authenticationSentManual && !pendingManualEmail.isEmpty()
            ? pendingManualEmail
            : CustomSettings::getUserEmail();

    if (email.isEmpty() || email == "Unknown user") {
        qDebug() << "[Auth] Polling stopped due to missing email context";
        stopPolling();
        return;
    }

    qDebug() << "[Auth] Polling auth status for session:" << currentSessionId;
    startRequest(email, currentDeviceName, currentMcuId);
}

void Authenticator::onFinished(QNetworkReply *reply)
{
    if (timeoutTimer) timeoutTimer->stop();
    if (reply != currentReply) { reply->deleteLater(); return; }
    currentReply = nullptr;
    const bool forceReconnect = authenticationSentManual;
    
    // Don't emit requestFinished during polling - keep the UI in "waiting" state
    if (!pollTimer->isActive()) {
        emit requestFinished();
    }

    if (reply->error() != QNetworkReply::NoError) {
        const QString e = reply->errorString();
        reply->deleteLater();
        // During polling, network errors don't stop polling - just log and continue
        if (pollTimer->isActive()) {
            qDebug() << "[Auth] Network error during polling:" << e << "- continuing";
            return;
        }
        emit authenticationFailed(QStringLiteral("network-error:%1").arg(e), QStringLiteral("Network error: %1").arg(e));
        return;
    }
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus != 200) {
        reply->deleteLater();
        // During polling, HTTP errors don't stop polling - just log and continue
        if (pollTimer->isActive()) {
            qDebug() << "[Auth] HTTP error during polling:" << httpStatus << "- continuing";
            return;
        }
        emit authenticationFailed(QStringLiteral("http-%1").arg(httpStatus), QStringLiteral("HTTP %1").arg(httpStatus));
        return;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    // Debug: Print the raw response
    qDebug() << "[Auth] Raw response from sniffy_auth_check.php:" << data;
    qDebug() << "[Auth] Response size:" << data.size() << "bytes";

    // Parse JSON response
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        // During polling, parsing errors don't stop polling
        if (pollTimer->isActive()) {
            qDebug() << "[Auth] JSON parse error during polling - continuing";
            return;
        }
        emit authenticationFailed("invalid-json", QObject::tr("Invalid JSON response: %1").arg(parseError.errorString()));
        qDebug() << "[Auth] JSON parse error:" << parseError.errorString() << "Data:" << data;
        return;
    }

    if (!jsonDoc.isObject()) {
        if (pollTimer->isActive()) {
            qDebug() << "[Auth] Invalid response format during polling - continuing";
            return;
        }
        emit authenticationFailed("invalid-response-format", QObject::tr("Invalid response format"));
        qDebug() << "[Auth] Response is not a JSON object";
        return;
    }

    QJsonObject jsonObj = jsonDoc.object();
    
    // Check for error responses
    if (jsonObj.contains("error")) {
        const QString serverError = jsonObj["error"].toString().trimmed();
        qDebug() << "[Auth] Error response received:" << serverError;

        // Error codes are wrapped in a random salt by sniffy_auth_check.php,
        // for example: AB-ERR-08-CD. Extract the stable ERR-xx code first.
        const QRegularExpression errorCodePattern(QStringLiteral("(?:^|-)\\b(ERR-[0-9]{2})\\b(?:-|$)"),
                               QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch errorCodeMatch = errorCodePattern.match(serverError);
        const QString errorType = errorCodeMatch.hasMatch()
            ? errorCodeMatch.captured(1).toUpper()
            : serverError;
        
        // Special handling during polling - "pending" or "waiting" errors mean keep polling
        if (pollTimer->isActive() && (errorType == "pending" || errorType == "waiting" || errorType == "not_authenticated")) {
            qDebug() << "[Auth] Still waiting for browser authentication...";
            return; // Continue polling
        }
        
        // Stop polling on actual errors
        if (pollTimer->isActive()) {
            stopPolling();
        }
        
        if (errorType == "ERR-08") {
            emit authenticationFailed(errorType, QObject::tr("Device limit exceeded. This account has reached its maximum number of registered devices."));
            return;
        } else if (errorType == "ERR-10") {
            emit authenticationFailed(errorType, QObject::tr("Your authentication session has expired. Please log in again on www.sniffylab.com."));
            return;
        } else if (errorType.compare("Expired", Qt::CaseInsensitive) == 0) {
            qDebug() << "[Auth] Token expired detected";
            emit authenticationFailed("expired", QObject::tr("Token expired – login on www.sniffylab.com first"));
            return;
        } else if (errorType.compare("Login", Qt::CaseInsensitive) == 0) {
            emit authenticationFailed("not_logged-in", QObject::tr("Log in on www.sniffylab.com as the same email. If another user is logged in, log out in browser first."));
            return;
        } else {
            emit authenticationFailed(errorType, QObject::tr("Authentication error: %1").arg(errorType));
            return;
        }
    }

    // Extract valid_till and token from JSON
    if (!jsonObj.contains("valid_till") || !jsonObj.contains("token")) {
        if (pollTimer->isActive()) {
            qDebug() << "[Auth] Missing fields during polling - continuing";
            return;
        }
        emit authenticationFailed("missing-fields", QObject::tr("Response missing required fields"));
        qDebug() << "[Auth] Missing valid_till or token in response";
        return;
    }

    const QString validityString = jsonObj["valid_till"].toString();
    const QString tokenString = jsonObj["token"].toString();
    
    if (validityString.isEmpty() || tokenString.isEmpty()) {
        if (pollTimer->isActive()) {
            return; // Continue polling
        }
        emit authenticationFailed("empty-fields", QObject::tr("Empty validity or token"));
        return;
    }

    // Parse the validity date - try ISO format first, then fallback to original format
    QDateTime validity = QDateTime::fromString(validityString, Qt::ISODate);
    if (!validity.isValid()) {
        validity = QDateTime::fromString(validityString, "yyyy-MM-dd hh:mm:ss");
    }
    if (!validity.isValid()) {
        validity = QDateTime::fromString(validityString, "yyyy-MM-dd");
    }
    
    const QByteArray token = tokenString.toLatin1();
    
    if (!validity.isValid() || token.isEmpty()) {
        if (pollTimer->isActive()) {
            return; // Continue polling
        }
        emit authenticationFailed("invalid-response-format", QObject::tr("Invalid response format"));
        qDebug() << "[Auth] Invalid validity date or empty token";
        return;
    }

    // SUCCESS! Stop polling if we were polling
    if (pollTimer->isActive()) {
        stopPolling();
    }

    QDateTime previousValidity = CustomSettings::getTokenValidity();

    // Manual login success should reopen the device once so the MCU receives
    // the new token. Background refreshes must not cause a reconnect loop.
    QString context = jsonObj["context"].toString();
    qDebug() << "[Auth] New Token generated: valid till" << validity.toString("yyyy-MM-dd") << "(Context:" << context << ") " << (forceReconnect ? "- forcing device reconnection" : "- no device reconnection");
    
    emit authenticationSucceeded(validity, token, forceReconnect);
    authenticationSentManual = false;
    pendingManualEmail.clear();
    if(!connectedInDemoMode){
        // request small popup only on automatic prolongation
        const QString tip = QObject::tr("Login valid till %1")
                                .arg(validity.date().toString("dd.MM.yyyy"));
        emit popupMessageRequested(tip);
    }
}
