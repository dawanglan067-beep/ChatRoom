#include "authdialog.h"
#include "authservice.h"
#include "mainwindow.h"

#include <QApplication>
#include <QSettings>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ChatRoom"));
    QCoreApplication::setApplicationName(QStringLiteral("ChatRoom"));

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
