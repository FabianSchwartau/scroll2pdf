#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QSettings>
#include <QScreen>
#include <QImage>
#include <QVector>
#include <QGraphicsScene>
#include <QElapsedTimer>

struct State
{
    QImage image;
    int imageHeightAlreadyProcessed;
    QVector<QImage> pages;
    bool autoScoll;
};

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;

    QScreen *screen;
    QSettings settings;
    QTimer timer;
    bool disableProfileLoad;
    bool disableProfileNameChanged;
    bool disableSettingsSave;
    int pageHeight;

    State state;
    QVector<State> oldStates;

    QGraphicsScene scene;
    QGraphicsPixmapItem *lastPage;

    QElapsedTimer undoBlockTimer;

    void splitIntoPages();
    void updateScene();
    void updateViewScale(int width);

private slots:
    void profileChanged(int);
    void buttonAdd();
    void buttonDelete();
    void takeScreenshot();
    void profileValueChanged();
    void buttonStart();
    void buttonUndo();
    void buttonSave();
    void viewScrollRangeChanged(int, int max);
    void viewScrollValueChaned(int val);
};
#endif // MAINWINDOW_H
