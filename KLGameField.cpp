//
// Created by esorochinskiy on 26.09.23.
//

#include <QPainter>
#include <QApplication>
#include<QDesktopWidget>
#include "KLGameField.h"
#include <QMouseEvent>
#include <QTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QDialogButtonBox>
#include <QPushButton>
#include "LoadGameException.h"
#include "configdialog.h"


#define FIELD_OFFSET 1
#define SPACE 1
#define MIN_CELL_SIZE 16
#define MAX_CELL_SIZE 128
#define DIVISOR 2000

KLGameField::KLGameField(const QColor &cellsColor, const QColor &backgroundColor,
                         const QColor &betweenColor,
                         int timerInterval, QWidget *parent) : QWidget(parent),
                                                               m_ColorCells(cellsColor),
                                                               m_ColorBackground(backgroundColor),
                                                               m_colorBetween(betweenColor),
                                                               evoTimer(new QTimer()) {
    m_cellSize = MIN_CELL_SIZE;
    setMouseTracking(true);
    changeMoveMode(false);
    m_Cursor = cursor();
    m_TimerInterval = DIVISOR / timerInterval;
    initTotalCells();
    connect(evoTimer, &QTimer::timeout, this, &KLGameField::nextGeneration);
}

KLGameField::~KLGameField() {

    delete m_MainLayer;
    delete m_NextStepLayer;
    delete evoTimer;
}

void KLGameField::restoreScreen() {
    m_cellSize = MIN_CELL_SIZE;
    m_CurrMemOffsetX = m_CurrMemOffsetY = 0;
    recalcScreenCells();
    repaint();
}


void KLGameField::changeDelta(int delta) {
    int tmpSize = 0;
    tmpSize = m_cellSize + delta;

    if (tmpSize > MAX_CELL_SIZE) {
        m_cellSize = MAX_CELL_SIZE;
    } else if (tmpSize < MIN_CELL_SIZE) {
        m_cellSize = MIN_CELL_SIZE;
    } else {
        m_cellSize = tmpSize;
    }
    recalcScreenCells();
    repaint();
}

void KLGameField::wheelEvent(QWheelEvent *event) {
    cancelTimerInstantly();
    QPoint numDegrees = event->angleDelta() / 8;
    changeDelta(numDegrees.y());

}

void KLGameField::paintEvent(QPaintEvent *e) {
    actualDoRePaint();
}

void KLGameField::actualDoRePaint() {

    QPainter painter(this);
    int h = height();
    int w = width();
    const QSize &fd = getStandardFieldDefs(w, h);
    painter.fillRect(FIELD_OFFSET + SPACE,FIELD_OFFSET + SPACE,fd.width(), fd.height(), m_colorBetween);

    const QPoint &mainOffset = getMainOffset();
    painter.translate(mainOffset.x(), mainOffset.y());
    for (int y = 0; y < m_ScrCellsY; ++y) {
        for (int x = 0; x < m_ScrCellsX; ++x) {
            QBrush color;

            color = (fromMainLayer(m_CurrMemOffsetX + x, m_CurrMemOffsetY + y)) ? QBrush(m_ColorCells) : QBrush(
                    m_ColorBackground);
            painter.fillRect(QRect(x * (m_cellSize + SPACE), y * (m_cellSize + SPACE),
                                   m_cellSize, m_cellSize), color);

        }

    }

    emit changeZoomIn(m_cellSize < MAX_CELL_SIZE);
    emit changeZoomOut(m_cellSize > MIN_CELL_SIZE);
    emit changeRestore(m_CurrMemOffsetX || m_CurrMemOffsetY || (m_cellSize != MIN_CELL_SIZE));

}

void KLGameField::recalcScreenCells() {

    int h = height();
    int w = width();
    const QSize &fd = getStandardFieldDefs(w, h);

    m_ScrCellsX = fd.width() / (m_cellSize + SPACE);
    m_ScrCellsY = fd.height() / (m_cellSize + SPACE);

    if (m_ScrCellsX > m_cellsX) {
        m_ScrCellsX = m_cellsX;
    }
    m_MaxMemOffsetX = m_cellsX - m_ScrCellsX;

    if (m_CurrMemOffsetX > m_MaxMemOffsetX) {
        m_CurrMemOffsetX = m_MaxMemOffsetX;
    }


    if (m_ScrCellsY > m_cellsY) {
        m_ScrCellsY = m_cellsY;
    }
    m_MaxMemOffsetY = m_cellsY - m_ScrCellsY;

    if (m_CurrMemOffsetY > m_MaxMemOffsetY) {
        m_CurrMemOffsetY = m_MaxMemOffsetY;
    }


    m_remScrX = (fd.width() - (m_cellSize + SPACE) * m_ScrCellsX) / 2;
    m_remScrY = (fd.height() - (m_cellSize + SPACE) * m_ScrCellsY) / 2;

}

void KLGameField::resizeEvent(QResizeEvent *event) {
    cancelTimerInstantly();

    recalcScreenCells();
    QWidget::resizeEvent(event);
}

uchar *KLGameField::initLayer(uchar *layer) {
    if (nullptr != layer) {
        delete layer;
    }

    layer = new uchar[m_cellsX * m_cellsY];
    memset(layer, 0, m_cellsX * m_cellsY);
    return layer;

}

void KLGameField::swapLayers(void) {

    uchar *tmp;
    tmp = m_MainLayer;
    m_MainLayer = m_NextStepLayer;
    m_NextStepLayer = tmp;
}

void KLGameField::recalculate(void) {

    int nEmpties = 0;
    for (int y = 0; y < m_cellsY; ++y) {
        for (int x = 0; x < m_cellsX; ++x) {

            nEmpties += (!fromMainLayer(x, y));

            int countNeighbors = calculateNeighbors(x, y);
            uchar targetVal;
            switch (countNeighbors) {
                case 3:
                    targetVal = 1;
                    break;
                case 2:
                    targetVal = fromMainLayer(x, y);
                    break;
                default:
                    targetVal = 0;
            }
            copyToLayer(m_NextStepLayer, x, y, targetVal);

        }
    }
    swapLayers();
    m_Generation++;

    if (nEmpties == m_cellsX * m_cellsY) {

        cancelTimerInstantly();
        emit emptyColony();
    } else {
        emit changeGeneration(m_Generation);
    }

}

int KLGameField::calculateNeighbors(int x, int y) {
    int locNeighbors = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {

            if (!dx && !dy) {
                continue;
            }

            int newY = y + dy;
            int newX = x + dx;

            if (newY < 0) {
                newY = m_cellsY - 1;
            }

            if (newY >= m_cellsY) {
                newY = 0;
            }


            if (newX < 0) {
                newX = m_cellsX - 1;
            }

            if (newX >= m_cellsX) {
                newX = 0;
            }

            locNeighbors += fromMainLayer(newX, newY);
        }
    }
    return locNeighbors;
}

uchar KLGameField::fromMainLayer(int x, int y) {
    return m_MainLayer[y * m_cellsX + x];
}

void KLGameField::copyToLayer(uchar *layer, int x, int y, uchar val) {
    layer[y * m_cellsX + x] = val;
}

QPoint KLGameField::getMainOffset() const {
    return {FIELD_OFFSET + SPACE + m_remScrX, FIELD_OFFSET + SPACE + m_remScrY};
}

void KLGameField::mouseDoubleClickEvent(QMouseEvent *event) {

    if (m_MoveMode) {
        return;
    }

    QPoint newPos = event->pos();
    if (!checkMousePosition(newPos)) {
        return;
    }

    cancelTimerInstantly();
    m_LeftbPressed = false;

    int cellX = newPos.x() / (m_cellSize + SPACE);
    int cellY = newPos.y() / (m_cellSize + SPACE);

    m_Generation = 0;
    copyToLayer(m_MainLayer, m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY,
                !fromMainLayer(m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY));
    repaint();
}

void KLGameField::mouseMoveEvent(QMouseEvent *event) {

    static int oldCellX = -1;
    static int oldCellY = -1;

    QPoint newPos = event->pos();
    if (!checkMousePosition(newPos)) {
        setCursor(m_Cursor);
        m_LeftbPressed = false;
        return;
    } else {

        if (m_LeftbPressed) {
            m_Generation = 0;
            cancelTimerInstantly();
            int cellX = newPos.x() / (m_cellSize + SPACE);
            int cellY = newPos.y() / (m_cellSize + SPACE);

            if (!m_MoveMode) {

                if (cellX != oldCellX || cellY != oldCellY) {
                    oldCellX = cellX;
                    oldCellY = cellY;
                    copyToLayer(m_MainLayer, m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY,
                                !fromMainLayer(m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY));
                    repaint();
                }
            } else {
                setCursor(Qt::ClosedHandCursor);
                if (cellX != oldCellX || cellY != oldCellY) {

                    if (oldCellX == -1 && oldCellY == -1) {
                        oldCellX = cellX;
                        oldCellY = cellY;
                        return;
                    }

                    int dirX = sgn(cellX - oldCellX);
                    int dirY = sgn(cellY - oldCellY);

                    oldCellX = cellX;
                    oldCellY = cellY;
                    intentToMoveField(dirX, dirY);
                }

            }
        } else {
            setCursor(m_MoveMode ? Qt::OpenHandCursor : Qt::CrossCursor);
        }
    }
}

void KLGameField::nextAction(bool) {
    recalculate();
    repaint();
}

void KLGameField::nextGeneration(void) {
    nextAction(true);
}

void KLGameField::checkTimerAndUpdate(bool) {
    if (!evoTimer->isActive()) {
        evoTimer->start(m_TimerInterval);
        emit changeControls(false);
    } else {
        evoTimer->stop();
        emit changeControls(true);
    }
}

void KLGameField::cancelTimerInstantly() {
    if (evoTimer->isActive()) {
        evoTimer->stop();
        emit changeControls(true);
    }

}

void KLGameField::timerChanged(int timerInterval) {
    m_TimerInterval = DIVISOR / timerInterval;
    if (evoTimer->isActive()) {
        evoTimer->stop();
        evoTimer->start(m_TimerInterval);
    }
}

void KLGameField::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPoint newPos = event->pos();
        if (!checkMousePosition(newPos)) {
            return;
        }
        cancelTimerInstantly();
        m_LeftbPressed = true;
    }
    QWidget::mousePressEvent(event);
}

void KLGameField::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPoint newPos = event->pos();
        if (!checkMousePosition(newPos)) {
            return;
        }
        cancelTimerInstantly();
        m_LeftbPressed = false;
    }

    QWidget::mouseReleaseEvent(event);
}

bool KLGameField::checkMousePosition(QPoint &mpos) const {
    mpos -= getMainOffset();
    return (mpos.x() >= 0 && mpos.y() >= 0 &&
            mpos.x() <= m_ScrCellsX * (m_cellSize + SPACE) && mpos.y() <= m_ScrCellsY * (m_cellSize + SPACE));
}

void KLGameField::newAction(bool) {
    cancelTimerInstantly();
    m_MainLayer = initLayer(m_MainLayer);
    m_NextStepLayer = initLayer(m_NextStepLayer);
    m_Generation = 0;
    emit changeGeneration(m_Generation);
    restoreScreen();
}

void KLGameField::openAction(bool) {
    cancelTimerInstantly();
    const QString &path = QFileDialog::getOpenFileName(this, tr("Load colony from file"),
                                                       QDir::homePath(), tr("This application (*.kgol)"));

    if ("" == path) {
        return;
    }

    tryLoadFromFile(path);
    restoreScreen();
}

void KLGameField::saveAction(bool) {
    cancelTimerInstantly();
    const QString &path = QFileDialog::getSaveFileName(this, tr("Save colony current state"),
                                                       QDir::homePath(), tr("This application (*.kgol)"));

    if ("" == path) {
        return;
    }


    try {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            throw LoadGameException(tr("Open file failed").toStdString());
        }
        QTextStream out(&file);

        out << "KGOL_SIG";
        for (int y = 0; y < m_cellsY; ++y) {
            for (int x = 0; x < m_cellsX; ++x) {
                if (fromMainLayer(x, y)) {
                    out << "CX" << x << "Y" << y;
                }
            }
        }

        file.close();
    } catch (const LoadGameException &ex) {
        QMessageBox::critical(this, tr("Error"), QString::fromStdString(ex.what()));
    }
}


void KLGameField::changeMoveMode(bool mMode) {
    cancelTimerInstantly();
    m_MoveMode = mMode;
    setStatusTip(!m_MoveMode ?
                 tr("Set or erase a single cell by double click or drag a line with left button pressed") :
                 tr("Drag the mouse to move field"));
}

int KLGameField::sgn(int val) {
    return (0 < val) - (val < 0);
}

void KLGameField::intentToMoveField(int dx, int dy) {
    bool changed = false;
    int tmpMemOffsetX = m_CurrMemOffsetX;
    int tmpMemOffsetY = m_CurrMemOffsetY;
    tmpMemOffsetX -= dx;
    tmpMemOffsetY -= dy;
    if (tmpMemOffsetX >= 0 && tmpMemOffsetX <= m_MaxMemOffsetX) {
        m_CurrMemOffsetX = tmpMemOffsetX;
        changed = true;
    }

    if (tmpMemOffsetY >= 0 && tmpMemOffsetY <= m_MaxMemOffsetY) {
        m_CurrMemOffsetY = tmpMemOffsetY;
        changed = true;
    }

    if (changed) {
        repaint();
    }
}

void KLGameField::initTotalCells() {

    QDesktopWidget *desktopwidget = QApplication::desktop(); // Get display resolution
    int dw = desktopwidget->width();
    int dh = desktopwidget->height();
    const QSize &fd = getStandardFieldDefs(dw, dh);

    m_cellsX = fd.width() / (m_cellSize + SPACE);
    m_cellsY = fd.height() / (m_cellSize + SPACE);
    m_MainLayer = initLayer(m_MainLayer);
    m_NextStepLayer = initLayer(m_NextStepLayer);
    m_Generation = 0;
    emit changeGeneration(m_Generation);

}

QSize KLGameField::getStandardFieldDefs(int &x, int &y) const {
    return {x - (FIELD_OFFSET + SPACE) * 2, y - (FIELD_OFFSET + SPACE) * 2};
}

void KLGameField::cZoomIn(bool) {
    cancelTimerInstantly();
    changeDelta(12);
}

void KLGameField::cZoomOut(bool) {
    cancelTimerInstantly();
    changeDelta(-12);
}

void KLGameField::cRestore(bool) {
    cancelTimerInstantly();
    restoreScreen();
}

void KLGameField::setupGame() {

    cancelTimerInstantly();
    std::unique_ptr<ConfigDialog> cDialog = std::make_unique<ConfigDialog>(m_ColorBackground,
                                                                           m_ColorCells, m_colorBetween, this);
    cDialog->setModal(true);
    int res = cDialog->exec();
    if (res == QDialog::Accepted) {
        applySetup(cDialog.get(), true);

    }
}

void KLGameField::tryLoadFromFile(const QString &path) {
    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            throw LoadGameException(tr("Open file failed").toStdString());
        }

        const QByteArray &header = file.read(8);
        const QString &strhead = QString::fromLatin1(header);
        if (strhead != "KGOL_SIG") {
            throw LoadGameException(tr("Invalid file format").toStdString());
        }


        QByteArray content;
        while (!file.atEnd()) {
            content = file.readAll();
        }
        file.close();
        const QString &strContent = QString::fromLatin1(content);
        const QStringList &coords = strContent.split("CX");
        if (coords.empty()) {
            throw LoadGameException(tr("Invalid file format").toStdString());
        }

        m_MainLayer = initLayer(m_MainLayer);
        m_NextStepLayer = initLayer(m_NextStepLayer);

        for (const QString &item: coords) {
            if (item.isEmpty()) {
                continue;
            } else if (!item.contains('Y')) {
                throw LoadGameException(tr("Invalid file format").toStdString());
            }

            const QStringList &pairXY = item.split('Y');
            bool ready = false;
            int resX;
            int resY = pairXY[1].toInt(&ready);
            if (ready) {
                resX = pairXY[0].toInt(&ready);
            }

            if (!ready) {
                throw LoadGameException(tr("Invalid file format").toStdString());
            }

            if (resX < 0 || resX >= m_cellsX || resY < 0 || resY >= m_cellsY) {
                continue;
            }


            copyToLayer(m_MainLayer, resX, resY, 1);
        }
    } catch (const LoadGameException &ex) {

        QMessageBox::critical(this, tr("Error"), QString::fromStdString(ex.what()));
    }
}

void KLGameField::cdApply(bool b) {

    auto *btn = dynamic_cast<QPushButton *>(sender());
    auto *cd = dynamic_cast<ConfigDialog *>(btn->parent()->parent());
    applySetup(cd, false);
    emit settingsApplied();
    repaint();
}

void KLGameField::applySetup(ConfigDialog *cDialog, bool confirm) {
    m_ColorCells = cDialog->getMCellColor();
    m_ColorBackground = cDialog->getMBackColor();
    m_colorBetween = cDialog->getMBetweenColor();
    emit changeSetting("cellsColor", m_ColorCells);
    emit changeSetting("backColor", m_ColorBackground);
    emit changeSetting("borderColor", m_colorBetween);

    const QString &tPath = cDialog->getTemplatePath();
    if (!tPath.isEmpty()) {
        if (confirm) {
            QMessageBox::StandardButton button = QMessageBox::warning(this, tr("Info"),
                                                                      tr("Do you really want to load "
                                                                         "colony from the selected template?"),
                                                                      QMessageBox::StandardButtons(
                                                                              QMessageBox::Yes | QMessageBox::No));

            if (button == QMessageBox::Yes) {
                tryLoadFromFile(tPath);
                restoreScreen();
            }
        } else {
            tryLoadFromFile(tPath);
            restoreScreen();
        }
    }
}







