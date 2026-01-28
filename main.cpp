#include "MainWindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    // 针对高DPI屏幕的适配 (虽然Win7通常不需要，但为了保险)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication a(argc, argv);

    // 设置全局字体，保证工控界面在低分屏上清晰
    QFont font("Microsoft YaHei", 9);
    a.setFont(font);

    MainWindow w;
    w.show();

    return a.exec();
}
