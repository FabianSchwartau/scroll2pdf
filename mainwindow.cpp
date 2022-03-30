#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QScreen>
#include <QVector>
#include <QPixmap>
#include <QColor>
#include <cmath>
#include <QPainter>
#include <QScrollBar>
#include <QFileDialog>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QPrinter>
#include <QInputDialog>
#include <QGraphicsPixmapItem>
#include <QTransform>
#include <QProcess>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , settings("Scroll2PDF", "Scroll2PDF", this)
    , disableProfileLoad(false)
    , disableProfileNameChanged(true)
    , disableSettingsSave(true)
{
    ui->setupUi(this);
    connect(ui->view->verticalScrollBar(), &QScrollBar::rangeChanged, this, &MainWindow::viewScrollRangeChanged);
    connect(ui->view->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::viewScrollValueChaned);

    screen = QGuiApplication::primaryScreen();

    QStringList groups = settings.childGroups();
    if(groups.size() == 0)
    {
        // Create a default profile
        QScreen *screen = QGuiApplication::primaryScreen();
        QPixmap screenshot = screen->grabWindow(0);
        settings.setValue("Default/offsetX", 0);
        settings.setValue("Default/offsetY", 0);
        settings.setValue("Default/sizeX", screenshot.width());
        settings.setValue("Default/sizeY", screenshot.height());
        settings.setValue("Default/maxError", 200);
        groups.append("Default");
    }

    QString selectedProfile = settings.value("selectedProfile", "Default").toString();
    if(!groups.contains(selectedProfile))
        selectedProfile = groups[0];
    ui->comboProfile->addItems(groups);
    ui->comboProfile->setCurrentIndex(ui->comboProfile->findText(selectedProfile));  // This will trigger profileChanged() to load profile data

    disableSettingsSave = false;
    disableProfileNameChanged = false;

    ui->view->setScene(&scene);
    state.autoScoll = true;
    state.imageHeightAlreadyProcessed = 0;
    updateScene();

    undoBlockTimer.start();

    connect(&timer, &QTimer::timeout, this, &MainWindow::takeScreenshot);
    timer.start(200);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::profileChanged(int)
{
    QString profile = ui->comboProfile->currentText();
    settings.setValue("selectedProfile", profile);

    if(disableProfileLoad)
        return;
    // Load the current values into the spin boxes
    disableSettingsSave = true;
    ui->spinOffsetX->setValue(settings.value(profile + "/offsetX", 0).toInt());
    ui->spinOffsetY->setValue(settings.value(profile + "/offsetY", 0).toInt());
    ui->spinSizeX->setValue(settings.value(profile + "/sizeX", 100).toInt());
    ui->spinSizeY->setValue(settings.value(profile + "/sizeY", 100).toInt());
    ui->spinMaxError->setValue(settings.value(profile + "/maxError", 50.0).toDouble());
    disableSettingsSave = false;
}

void MainWindow::buttonAdd()
{
    QString profileName = QInputDialog::getText(this, "New Profile", "Profile Name");
    if(profileName.isEmpty())
        return;
    if(settings.childGroups().contains(profileName))
    {
        QMessageBox::critical(this, "Error", "Profile name already taken");
        return;
    }
    ui->comboProfile->addItem(profileName);
    disableProfileLoad = true;
    ui->comboProfile->setCurrentIndex(ui->comboProfile->count()-1);
    disableProfileLoad = false;

    profileValueChanged();
}

void MainWindow::buttonDelete()
{
    if(settings.childGroups().size() == 1)
    {
        QMessageBox::critical(this, "Error", "Cannot delete last profile");
        return;
    }
    if(QMessageBox::question(this, "Delete Profile", "Sure?", QMessageBox::Yes|QMessageBox::No) != QMessageBox::Yes)
        return;
    settings.remove(ui->comboProfile->itemText(ui->comboProfile->currentIndex()));
    ui->comboProfile->removeItem(ui->comboProfile->currentIndex());
}

void MainWindow::takeScreenshot()
{
    // Make a screenshot
    QPixmap screenshot = screen->grabWindow(0, ui->spinOffsetX->value(), ui->spinOffsetY->value(), ui->spinSizeX->value(), ui->spinSizeY->value());
    QImage screenshotImg = screenshot.toImage();

    updateViewScale(screenshot.width());

    if(ui->buttonStart->text() == "Start")
    {
        // Just display current selected area
        lastPage->setPixmap(screenshot);
        scene.setSceneRect(0, 0, screenshot.width(), screenshot.height());
    }
    else
    {
        // If the undo button has been pressen in the last 500 ms, we don't process anything to give the user
        // the oportunity to press it again and again to delete multiple steps without adding anything new.
        if(undoBlockTimer.elapsed() < 500)
            return;
        // Figure out the shift since the last screenshot
        // Sweep over the shift value and calculate the error for each shift value as "meas square of differences" along the y-axis
        // TODO: This "RMS error" calculation is very inefficient. That's why x und y are incremented by 2, to reduce the CPU load
        // TODO: A more efficient way may be to use FFT based correlation, but that is much more complex, requires other libraries, ...
        QElapsedTimer elapsed;
        elapsed.start();
        int bestIdx = -1;
        double bestError = 0.0;
        for(int shift = 0; shift < screenshotImg.height() - 200; shift++)
        {
            // Cut the two overlapping parts
            int h = screenshotImg.height() - shift;
            int w = state.image.width();
            int offsetYRef = state.image.height() - h;
            long error = 0;
            for(int y = 0; y < h; y+=2)
            {
                QRgb *rgb1=(QRgb*)state.image.constScanLine(offsetYRef + y);
                QRgb *rgb2=(QRgb*)screenshotImg.constScanLine(y);
                for(int x = 0; x < w; x+=2)
                {
                    long errR = qRed(rgb1[x]) - qRed(rgb2[x]);
                    long errG = qGreen(rgb1[x]) - qGreen(rgb2[x]);
                    long errB = qBlue(rgb1[x]) - qBlue(rgb2[x]);
                    error += errR*errR + errG*errG + errB*errB;
                }
            }
            if(bestIdx == -1 || (double)error/(w*h) < bestError)
            {
                bestError = (double)error/(w*h);
                bestIdx = shift;
            }

            // If we found a perfect match, just break to save CPU power
            if(error == 0)
                break;
        }
        ui->labelErrorCurrent->setText(QString::number(bestError, 'f', 2));

        // Update GUI
        ui->labelFrameTime->setText(QString::number(elapsed.elapsed(), 'f', 0) + " ms");

        // If we have not moved, don't do anything
        if(bestIdx == 0)
            return;

        if(bestError < ui->spinMaxError->value())
        {
            // Backup the old state
            oldStates.append(state);
            if(oldStates.size() > 30)
                oldStates.pop_front();
            ui->buttonUndo->setEnabled(true);

            // Append the new image
            QImage imgAppend = screenshotImg.copy(0, screenshotImg.height() - bestIdx, screenshotImg.width(), bestIdx);
            state.image = QImage(oldStates.last().image.width(), oldStates.last().image.height() + imgAppend.height(), oldStates.last().image.format());
            QPainter painter(&state.image);
            painter.drawImage(0, 0, oldStates.last().image);
            painter.drawImage(0, oldStates.last().image.height(), imgAppend);
            painter.end();

            splitIntoPages();
        }
    }
}

void MainWindow::splitIntoPages()
{
    // Now process the new image into pages
    while(state.image.height() - state.imageHeightAlreadyProcessed >= pageHeight)
    {
        // Copy the full page that we may append
        QImage page = state.image.copy(0, state.imageHeightAlreadyProcessed, state.image.width(), pageHeight);

        // See if we can find a better cut for the page
        long bestRowBrighness = 0;
        int bestOffset = 0;
        for(int y = 200; y >= 0; y--)
        {
            QRgb *rgb=(QRgb*)page.constScanLine(page.height() - y - 1);
            long rowBrightness = 0;
            for(int x = 0; x < page.width(); x++)
                rowBrightness += qRed(rgb[x]) + qGreen(rgb[x]) + qBlue(rgb[x]);
            if(rowBrightness >= bestRowBrighness)
            {
                bestRowBrighness = rowBrightness;
                bestOffset = y;
            }
        }
        page = page.copy(0, 0, page.width(), page.height() - bestOffset);

        // Append the new page to the list
        state.pages.append(page);
        state.imageHeightAlreadyProcessed += page.height();
    }

    // Update page count in GUI
    ui->labelPages->setText(QString::number(state.pages.size() + 1));

    // Limit image height to 2000 px, as QImage only supports 2^15-1 pixels in each axis
    if(state.image.height() > 2000)
    {
        const int cropHeight = state.image.height() - 2000;
        state.image = state.image.copy(0, cropHeight, state.image.width(), 2000);
        state.imageHeightAlreadyProcessed -= cropHeight;
    }

    // Replot the scene
    // TODO: This is maybe not the efficient way of updating the scene as this will clear the scene and
    // redraw it completely. But otherwise we would have to keep track what we are doing to support the
    // undo button. So this is the easier solution.
    updateScene();
}

void MainWindow::updateScene()
{
    // Clear scene and plot pages
    scene.clear();
    lastPage = new QGraphicsPixmapItem();
    lastPage->setOffset(0, 0);
    lastPage->setTransformationMode(Qt::SmoothTransformation);
    scene.addItem(lastPage);
    int totalPixelLength = 0;
    for(int i = 0; i < state.pages.size(); i++)
    {
        QGraphicsPixmapItem *item = new QGraphicsPixmapItem(QPixmap::fromImage(state.pages[i]));
        item->setTransformationMode(Qt::SmoothTransformation);
        item->setOffset(0, totalPixelLength);
        scene.addItem(item);
        totalPixelLength += state.pages[i].height() + 20;
    }

    // Plot the last (uncomplete) page
    lastPage->setPixmap(QPixmap::fromImage(state.image.copy(0, state.imageHeightAlreadyProcessed, state.image.width(), state.image.height() - state.imageHeightAlreadyProcessed)));
    lastPage->setOffset(0, totalPixelLength);

    scene.setSceneRect(0, 0, state.image.width(), totalPixelLength+lastPage->pixmap().height()+20);
}

void MainWindow::updateViewScale(int width)
{
    // Calculate the view scale
    double scale = (double)(ui->view->width()-50)/(double)width;
    //ui->view->resetTransform();
    //ui->view->scale(scale, scale);
    QTransform tf;
    tf.scale(scale, scale);
    if(ui->view->transform() != tf)
        ui->view->setTransform(tf);
}

void MainWindow::profileValueChanged()
{
    if(disableSettingsSave)
        return;
    if(ui->spinOffsetX->value() + ui->spinSizeX->value() > screen->geometry().width())
        ui->spinSizeX->setValue(screen->geometry().width() - ui->spinOffsetX->value());
    if(ui->spinOffsetY->value() + ui->spinSizeY->value() > screen->geometry().height())
        ui->spinSizeY->setValue(screen->geometry().height() - ui->spinOffsetY->value());
    settings.setValue(ui->comboProfile->currentText() + "/offsetX", ui->spinOffsetX->value());
    settings.setValue(ui->comboProfile->currentText() + "/offsetY", ui->spinOffsetY->value());
    settings.setValue(ui->comboProfile->currentText() + "/sizeX", ui->spinSizeX->value());
    settings.setValue(ui->comboProfile->currentText() + "/sizeY", ui->spinSizeY->value());
    settings.setValue(ui->comboProfile->currentText() + "/maxError", ui->spinMaxError->value());
}

void MainWindow::buttonStart()
{
    if(ui->buttonStart->text() == "Start")
    {
        ui->buttonStart->setText("Stop");
        ui->spinOffsetX->setEnabled(false);
        ui->spinOffsetY->setEnabled(false);
        ui->spinSizeX->setEnabled(false);
        ui->spinSizeY->setEnabled(false);
        ui->buttonUndo->setEnabled(false);
        ui->comboProfile->setEnabled(false);
        ui->buttonAdd->setEnabled(false);
        ui->buttonDelete->setEnabled(false);
        ui->buttonSave->setEnabled(true);

        // Reset state, history, and scene
        state.imageHeightAlreadyProcessed = 0;
        state.autoScoll = true;
        state.pages.clear();
        oldStates.clear();

        // Load first screenshot
        QPixmap screenshot = screen->grabWindow(0, ui->spinOffsetX->value(), ui->spinOffsetY->value(), ui->spinSizeX->value(), ui->spinSizeY->value());
        state.image = screenshot.toImage();

        // Calculate the page height in pixels based on the default printer settings
        QPrinter printer(QPrinter::PrinterResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setPageSize(QPageSize(QPageSize::A4));
        QRectF printerSize = printer.pageRect(QPrinter::Inch);
        pageHeight = (int)((double)screenshot.width()*printerSize.height()/printerSize.width());

        // And process the first screenshot
        splitIntoPages();
        updateScene();
        updateViewScale(screenshot.width());
    }
    else
    {
        ui->buttonStart->setText("Start");
        ui->spinOffsetX->setEnabled(true);
        ui->spinOffsetY->setEnabled(true);
        ui->spinSizeX->setEnabled(true);
        ui->spinSizeY->setEnabled(true);
        ui->buttonUndo->setEnabled(false);
        ui->comboProfile->setEnabled(true);
        ui->buttonAdd->setEnabled(true);
        ui->buttonDelete->setEnabled(true);
        scene.clear();
        lastPage = new QGraphicsPixmapItem();
        lastPage->setOffset(0, 0);
        lastPage->setTransformationMode(Qt::SmoothTransformation);
        scene.addItem(lastPage);
        ui->view->verticalScrollBar()->setValue(ui->view->verticalScrollBar()->minimum());
    }
}

void MainWindow::buttonUndo()
{
    if(oldStates.size() == 0)
        return;
    state = oldStates.last();
    oldStates.pop_back();

    // Update the scene
    updateScene();

    if(oldStates.size() == 0)
        ui->buttonUndo->setEnabled(false);

    // Block updates for the next 500 ms
    undoBlockTimer.restart();
}

void MainWindow::buttonSave()
{
    QString file = QFileDialog::getSaveFileName(this, "Save document", settings.value("lastFile", ".").toString(), "PDF (*.pdf);;PNG (*.png)");
    if(file.isEmpty())
        return;
    settings.setValue("lastFile", file);

    if(file.toLower().endsWith(".pdf"))
    {
        QPrinter printer(QPrinter::PrinterResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setPageSize(QPageSize(QPageSize::A4));
        printer.setOutputFileName(file);
        printer.setResolution((int)((double)state.image.width()/printer.pageRect(QPrinter::Inch).width()));

        QPainter painter;
        painter.begin(&printer);
        for(int i = 0; i < state.pages.size(); i++)
        {
            painter.drawImage(0, 0, state.pages[i]);
            if(i + 1 != state.pages.size())
                printer.newPage();
        }
        if(state.image.height() - state.imageHeightAlreadyProcessed > 0)
        {
            printer.newPage();
            painter.drawImage(0, 0, state.image.copy(0, state.imageHeightAlreadyProcessed, state.image.width(), state.image.height() - state.imageHeightAlreadyProcessed));
        }
        painter.end();

#if (defined (LINUX) || defined (__linux__))
        // Run ocrmypdf in terminal window
        if(QMessageBox::question(this, "Run OCR", "Run ocrmypdf on the file?", QMessageBox::Yes|QMessageBox::No) != QMessageBox::Yes)
            return;
        QProcess process;
        process.start("/usr/bin/xterm", {"-e", QString("ocrmypdf \"") + file + "\" \"" + file + "\""});
        process.waitForFinished(-1);
        // TODO: This does not work: The exit code only refers to xterm. If ocrmypdf was not found or failed, this will return 0.
        if(process.exitCode() != 0)
            QMessageBox::critical(this, "ocrmypdf failed", "ocrmypdf returned with an error code, is it installed and working? Dou you have xterm?");
#endif
#if (defined (_WIN32) || defined (_WIN64))
        // TODO: I don't know - I don't have a windows build machine...
#endif
    }
    else if(file.endsWith(".png", Qt::CaseInsensitive))
    {
        file.chop(4); // Remve ".png" from the end
        for(int i = 0; i < state.pages.size(); i++)
        {
            QString fileNum = file + "-" + QString("%1").arg(i, 4, 10, QChar('0')) + ".png";
            state.pages[i].save(fileNum);
        }
        QString fileNum = file + "-" + QString("%1").arg(state.pages.size(), 4, 10, QChar('0')) + ".png";
        state.image.copy(0, state.imageHeightAlreadyProcessed, state.image.width(), state.image.height() - state.imageHeightAlreadyProcessed).save(fileNum);
    }
    else
        QMessageBox::critical(this, "Unknown file format", "Unknown file format/ending");
}

void MainWindow::viewScrollRangeChanged(int, int max)
{
    if(!state.autoScoll)
        return;
    ui->view->verticalScrollBar()->setValue(max);
}

void MainWindow::viewScrollValueChaned(int val)
{
    if(val == ui->view->verticalScrollBar()->maximum())
        state.autoScoll = true;
    else
        state.autoScoll = false;
}
