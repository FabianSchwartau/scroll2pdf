QT       += core gui printsupport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h

FORMS += \
    mainwindow.ui

isEmpty(PREFIX) {
  PREFIX=/usr
}

unix: target.path = $$PREFIX/bin/
!isEmpty(target.path): INSTALLS += target

unix:  desktop.path  = $$PREFIX/share/applications
unix:  desktop.files = scroll2pdf.desktop
unix:  icons.path    = $$PREFIX/share/icons/hicolor/256x256/apps/
unix:  icons.files   = scroll2pdf.png
unix:  INSTALLS     += desktop icons

QMAKE_CXXFLAGS_RELEASE += -O2 -Wall
QMAKE_CXXFLAGS_DEBUG += -O2 -Wall

RC_ICONS += scroll2pdf.ico

RESOURCES += scroll2pdf.qrc




