#include "authdialog.h"
#include "authservice.h"
#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ChatRoom"));
    QCoreApplication::setApplicationName(QStringLiteral("ChatRoom"));

    const QString appDir = QCoreApplication::applicationDirPath();
    QCoreApplication::addLibraryPath(appDir);

    AuthService authService;
    AuthDialog authDialog(&authService);
    if (authDialog.exec() != QDialog::Accepted) {
        return 0;
    }

    MainWindow w;
    w.setAuthSession(authService.backendBaseUrl(),
                     authService.authToken(),
                     authDialog.authenticatedEmail());
    w.show();
    return a.exec();
}
