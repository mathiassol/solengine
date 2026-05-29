#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QString>
#include "main_window.h"
#include "project_launcher.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("SolEngine Editor");
    app.setOrganizationName("SolEngine");
    app.setApplicationVersion("0.1.0");

    QFont f("Segoe UI", 10);
    app.setFont(f);

    QFile styleFile(":/style.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    // Optional project path as first positional argument
    QString projectDir;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (!arg.startsWith('-')) {
            projectDir = arg;
            break;
        }
    }

    // If no valid project was given on the command line, show the launcher
    if (projectDir.isEmpty() || !QFileInfo(projectDir + "/project.sol").exists()) {
        ProjectLauncherDialog launcher;
        if (launcher.exec() != QDialog::Accepted)
            return 0;  // user cancelled
        projectDir = launcher.selectedProject();
    }

    MainWindow w(projectDir);
    w.show();

    return app.exec();
}
