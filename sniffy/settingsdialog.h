#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QSettings>
#include <QFile>
#include <QDebug>
#include <QMessageBox>

#include "GUI/widgetbuttons.h"
#include "GUI/widgetselection.h"
#include "GUI/widgetseparator.h"
#include "GUI/widgetlabel.h"
#include "GUI/simplehprogressbar.h"
#include <QPlainTextEdit>
#include "customsettings.h"
#include "firmwaremanager.h"

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(Authenticator *auth, QWidget *parent = nullptr);
    ~SettingsDialog();
    void open();

private:
    Ui::SettingsDialog *ui;

    WidgetButtons *buttonsRestoreSession;
    WidgetButtons *buttonsSmartSessionGeometry;
    WidgetButtons *buttonsSession;
    WidgetSelection * selTheme;
    WidgetSelection * selFlashSource; // Local vs Remote firmware source

    // Flasher widgets
    WidgetButtons *buttonsFlash;
    WidgetButtons *buttonsErase;
    WidgetButtons *buttonsLicenses;
    WidgetButtons *buttonsCheckUpdates;
    SimpleHProgressBar *flashProgressBar;
    QPlainTextEdit *flashLogWindow;
    
    FirmwareManager *m_firmwareManager;

    WidgetLabel *infoLabel;
    WidgetLabel *labelUpdateStatus;
    WidgetButtons *buttonsDone;

    bool restartNeededWarning = false;

public slots:
    void closeDialog(int isCanceled);
    void onUserLoginChanged();
    void setUpdateStatusText(const QString &text);

private:
    enum class FirmwareUiOperation {
        None,
        Flash,
        Erase
    };

    int m_lastStatusType = 0;
    FirmwareUiOperation m_lastFirmwareOperation = FirmwareUiOperation::None;

private slots:
    void restartWarning();
    void sessionButtonClicked(int index, int optionalEmitParam = 0);
    void onFlashButtonClicked(int index, int optionalEmitParam = 0);
    void onEraseButtonClicked(int index, int optionalEmitParam = 0);
    void showThirdPartyLicenses();
    
    // Firmware Manager slots
    void onFirmwareProgress(int value, int total);
    void onFirmwareStatusMessage(const QString &msg, const QColor &color, int msgType);
    void onFirmwareLogMessage(const QString &message);
    void onFirmwareOperationStarted();
    void onFirmwareOperationFinished(bool success);
    void onFirmwareFlashed();

signals:
    void saveSessionRequested();
    void loadSessionRequested();
    void checkForUpdatesRequested();
    void firmwareFlashed();
    void massEraseRequested();
    void massEraseCompleted();
};

#endif // SETTINGSDIALOG_H
