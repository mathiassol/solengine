#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QString>
#include "main_window.h"

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
    if (projectDir.isEmpty())
        projectDir = QDir::currentPath();

    MainWindow w(projectDir);
    w.show();

    return app.exec();
}
