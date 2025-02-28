//
// Created by esorochinskiy on 26.09.23.
//

#include <QPainter>
#include <QPrinter>
#include <QPrintDialog>

#include <QApplication>
#include <QComboBox>
#include <KActionCollection>
#include <QDesktopWidget>
#include "KLGameField.h"
#include <KXMLGUIFactory>
#include <QMouseEvent>
#include <QTimer>
#include <QAction>
#include <QFileDialog>
#include <KMessageBox>
#include <KLocalizedString>
#include <KConfigDialog>
#include <QBuffer>
#include <memory>
#include "LoadGameException.h"
#include "kglife.h"
#include "generalpage.h"
#include "patternspage.h"
#include "generatorpage.h"
#include "AnalysysDialog.h"

#define FIELD_OFFSET 1
#define SPACE 1
#define MIN_CELL_SIZE 16
#define MAX_CELL_SIZE 128
#define DIVISOR 2000

QString KLGameField::CurrentFilePath = "";

KLGameField::KLGameField(int timerInterval, QWidget *parent) : QWidget(parent),
                                                               KXMLGUIClient(),
                                                               mGenerator(new CellsGenerator()),
                                                               evoTimer(new QTimer()) {
    m_cellSize = MIN_CELL_SIZE;
    setMouseTracking(true);
    changeMoveMode(false);
    setContextMenuPolicy(Qt::CustomContextMenu);
    m_Cursor = cursor();
    m_TimerInterval = DIVISOR / timerInterval;
    initTotalCells();
    connect(evoTimer, &QTimer::timeout, this, &KLGameField::nextGeneration);
    connect(mGenerator, &CellsGenerator::resultReady, this, &KLGameField::generatorReady);
    connect(mGenerator, &CellsGenerator::resultFinished, this, [=]() {
        repaint();
    });

    setupCommonActions();
    setupFactory();
    connect(this, &QWidget::customContextMenuRequested, this, &KLGameField::showContextMenu);
}

KLGameField::~KLGameField() {

    reallocAllLayers();
    delete evoTimer;
    delete mGenerator;

    if (factory()) {
        factory()->removeClient(this);
    }

    delete _clientBuilder;
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

void KLGameField::paintEvent(QPaintEvent *) {
    actualDoRePaint();
}

void KLGameField::actualDoRePaint() {

    QPainter painter(this);
    int h = height();
    int w = width();
    const QSize &fd = getStandardFieldDefs(w, h);
    painter.fillRect(FIELD_OFFSET + SPACE, FIELD_OFFSET + SPACE, fd.width(), fd.height(), Settings::bordercolor());

    const QPoint &mainOffset = getMainOffset();
    painter.translate(mainOffset.x(), mainOffset.y());
    bool hasSelected = false;
    for (int y = 0; y < m_ScrCellsY; ++y) {
        for (int x = 0; x < m_ScrCellsX; ++x) {
            QBrush color;
            hasSelected = (hasSelected || fromLayer(m_SelectionLayer, m_CurrMemOffsetX + x, m_CurrMemOffsetY + y));
            color = (fromMainLayer(m_CurrMemOffsetX + x, m_CurrMemOffsetY + y)) ? (
                    fromLayer(m_SelectionLayer, m_CurrMemOffsetX + x, m_CurrMemOffsetY + y) ? Settings::selectedcolor()
                                                                                            : Settings::cellcolor()
            ) : Settings::backcolor();
            painter.fillRect(QRect(x * (m_cellSize + SPACE), y * (m_cellSize + SPACE),
                                   m_cellSize, m_cellSize), color);

        }

    }

    if (hasSelected) {
        QPoint minOrigin = QPoint(m_cellsX, m_cellsY);
        QPoint maxOrigin = QPoint(0, 0);

        doMiniMaxTestsOnLayer(m_SelectionLayer, minOrigin, maxOrigin);
        if (!maxOrigin.isNull()) {
            painter.setPen(QPen(Settings::borderselectioncolor()));
            auto startPoint = (minOrigin - QPoint(m_CurrMemOffsetX, m_CurrMemOffsetY)) * (m_cellSize + SPACE);
            auto finalPoint =
                    (maxOrigin + QPoint(1, 1) - QPoint(m_CurrMemOffsetX, m_CurrMemOffsetY)) * (m_cellSize + SPACE) -
                    QPoint(SPACE * 2, SPACE * 2);
            painter.drawRect(QRect(startPoint, finalPoint));
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

void KLGameField::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    cdApply(QString());
}

uchar *KLGameField::initLayer(uchar *layer) const {

    if (!layer) {
        layer = new uchar[m_cellsX * m_cellsY];
    }
    memset(layer, 0, m_cellsX * m_cellsY);
    return layer;

}

void KLGameField::swapLayers() {

    uchar *tmp;
    tmp = m_MainLayer;
    m_MainLayer = m_NextStepLayer;
    m_NextStepLayer = tmp;
}

void KLGameField::recalculate() {

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
    bool isFieldToroidal = Settings::toroidal();
    bool isFieldWalled = Settings::walled();
    bool isFieldInfinite = Settings::infinite();
    int locNeighbors = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {

            if (!dx && !dy) {
                continue;
            }

            int newY = y + dy;
            int newX = x + dx;

            if (isFieldToroidal) {
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
            } else if (isFieldWalled) {
                if (newY < 0 || newY >= m_cellsY || newX < 0 || newX >= m_cellsX) {
                    continue;
                } else {
                    locNeighbors += fromMainLayer(newX, newY);
                }
            } else if (isFieldInfinite) {
                if (newY < 0 || newY >= m_cellsY || newX < 0 || newX >= m_cellsX) {
                    locNeighbors = 0;
                } else {
                    locNeighbors += fromMainLayer(newX, newY);
                }
            }
        }
    }
    return locNeighbors;
}

uchar KLGameField::fromMainLayer(int x, int y) {
    return fromLayer(m_MainLayer, x, y);
}

uchar KLGameField::fromLayer(uchar *layer, int x, int y) const {
    return layer[y * m_cellsX + x];
}

void KLGameField::copyToLayer(uchar *layer, int x, int y, uchar val) const {
    layer[y * m_cellsX + x] = val;
}

QPoint KLGameField::getMainOffset() const {
    return {FIELD_OFFSET + SPACE + m_remScrX, FIELD_OFFSET + SPACE + m_remScrY};
}

void KLGameField::mouseDoubleClickEvent(QMouseEvent *event) {

    if (event->button() != Qt::LeftButton) {
        return;
    }

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

    if (m_SelectionMode) {
        if (fromMainLayer(m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY)) {
            copyToLayer(m_SelectionLayer, m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY,
                        !fromLayer(m_SelectionLayer, m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY));
        }
    } else {
        copyToLayer(m_MainLayer, m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY,
                    !fromMainLayer(m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY));
        copyToLayer(m_SelectionLayer, m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY, 0);
    }
    repaint();
}

void KLGameField::mouseMoveEvent(QMouseEvent *event) {

    static int oldCellX = -1;
    static int oldCellY = -1;
    const QCursor &selectCursor = QCursor(QPixmap(":/images/cursor.png"));

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

                    if (m_SelectionMode) {
                        if (fromMainLayer(m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY)) {
                            copyToLayer(m_SelectionLayer, m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY,
                                        !fromLayer(m_SelectionLayer, m_CurrMemOffsetX + cellX,
                                                   m_CurrMemOffsetY + cellY));
                        }
                    } else {
                        copyToLayer(m_MainLayer, m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY,
                                    !fromMainLayer(m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY));
                        copyToLayer(m_SelectionLayer, m_CurrMemOffsetX + cellX, m_CurrMemOffsetY + cellY, 0);
                    }
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
            setCursor(m_MoveMode ? Qt::OpenHandCursor : (m_SelectionMode ? selectCursor : Qt::CrossCursor));
        }
    }
}

void KLGameField::nextAction() {
    recalculate();
    repaint();
}

void KLGameField::nextGeneration() {
    nextAction();
}

void KLGameField::singleAction(bool) {
    clearSelection();
    nextAction();
}

void KLGameField::checkTimerAndUpdate(bool) {
    clearSelection();
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
    initLayers();
    m_Generation = 0;
    emit changeGeneration(m_Generation);
    restoreScreen();
}

void KLGameField::openAction(bool) {
    cancelTimerInstantly();
    const QString &filters = i18n("This application (*.kgol);;"
                                  "Object Vector Format (*.ovf);;"
                                  "Life32/XLife Run Length Encoding (*.rle);;"
                                  "Plaintext (*.cells);;All files (*.*)");
    QString fStr = filters.split(";;").at(0);
    const QString &path = QFileDialog::getOpenFileName(parentWidget(), i18n("Load colony from file"),
                                                       QDir::homePath(), filters, &fStr);

    if (path.isEmpty()) {
        return;
    }

    setCurrentPath(this, path);
    tryLoadFromFile(path);
    restoreScreen();
}

QString KLGameField::forceFileNameDialog() {
    const QString &filters = i18n("This application (*.kgol);;"
                                  "Life32/XLife Run Length Encoding (*.rle);;"
                                  "Plaintext (*.cells)");
    QString fStr = filters.split(";;").at(0);
    QString path = QFileDialog::getSaveFileName(parentWidget(), i18n("Save colony current state"),
                                                CurrentFilePath.isEmpty() ? QDir::homePath() :
                                                QFileInfo(CurrentFilePath).dir().path(),
                                                filters, &fStr);
    return path;
}


void KLGameField::saveAsAction(bool) {
    cancelTimerInstantly();
    const QString &path = forceFileNameDialog();

    if (path.isEmpty()) {
        return;
    }
    setCurrentPath(this, path);
    trySaveToFile(path);
}

void KLGameField::saveAction(bool) {
    cancelTimerInstantly();
    if (CurrentFilePath.isEmpty()) {
        const QString &path = forceFileNameDialog();
        if (path.isEmpty()) {
            return;
        }
        setCurrentPath(this, path);
    }
    trySaveToFile(CurrentFilePath);

}

void KLGameField::setCurrentPath(KLGameField *th, const QString &cpath) {
    if (!cpath.isEmpty()) {
        CurrentFilePath = cpath;
        emit th->changeCurrentFile(QFileInfo(cpath).fileName());
    }
}

void KLGameField::trySaveToFile(const QString &path) {
    trySaveToFileLayer(m_MainLayer, path);
}


void KLGameField::trySaveToFileLayer(uchar *layer, const QString &path) {

    QFileInfo fileInfo(path);
    if (fileInfo.suffix().toLower() == "rle") {
        tryToExportFromLayerRLE(layer, path);
    } else if (fileInfo.suffix().toLower() == "cells") {
        tryToExportFromLayerCells(layer, path);
    } else {
        tryToExportFromLayerNative(layer, path);
    }
}


void KLGameField::changeMoveMode(bool mMode) {
    cancelTimerInstantly();
    m_MoveMode = mMode;
    const QString &trans = !m_MoveMode ?
                           i18n("Set or erase a single cell by double click or drag a line with left button pressed") :
                           i18n("Drag the mouse to move field");
    const QString &whats = !m_MoveMode ? i18n(
            "Put any cells here by mouse click or simply drag a line of cells.<br>You can also "
            "zoom in and out with a mouse wheel or action controls") :
                           i18n("In move mode you simply drag the mouse to move the field and see the cells beyond the edges");
    setStatusTip(trans);
    setToolTip(trans);
    setWhatsThis(whats);
}

void KLGameField::changeSelectMode(bool mMode) {
    cancelTimerInstantly();
    m_SelectionMode = mMode;

    const QString &trans = !m_SelectionMode ?
                           i18n("Set or erase a single cell by double click or drag a line with left button pressed") :
                           i18n("Drag the mouse to select the live cells");
    const QString &whats = !m_SelectionMode ? i18n(
            "Put any cells here by mouse click or simply drag a line of cells.<br>You can also "
            "zoom in and out with a mouse wheel or action controls") :
                           i18n("In select mode you simply drag the mouse to select the live cells");
    setStatusTip(trans);
    setToolTip(trans);
    setWhatsThis(whats);
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

void KLGameField::reallocAllLayers() {
    delete m_SelectionLayer;
    m_SelectionLayer = nullptr;
    delete m_MainLayer;
    m_MainLayer = nullptr;
    delete m_NextStepLayer;
    m_NextStepLayer = nullptr;
}

void KLGameField::initTotalCells() {

    QDesktopWidget *desktopwidget = QApplication::desktop(); // Get display resolution
    int dw = desktopwidget->width();
    int dh = desktopwidget->height();
    const QSize &fd = getStandardFieldDefs(dw, dh);

    m_cellsX = fd.width() / (m_cellSize + SPACE);
    m_cellsY = fd.height() / (m_cellSize + SPACE);

    bool isInfinite = Settings::infinite();
    if (isInfinite) {
        m_cellsX *= 2;
        m_cellsY *= 2;
    }
    m_isInfinite = isInfinite;

    initLayers();
    m_Generation = 0;
    emit changeGeneration(m_Generation);

}

QSize KLGameField::getStandardFieldDefs(int &x, int &y) {
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
    auto *dialog = KConfigDialog::exists(QStringLiteral("Settings"));
    if (!dialog) {
        dialog = new KConfigDialog(parentWidget(), QStringLiteral("Settings"), Settings::self());
        QSize ds = {940, 630};
        dialog->setMinimumSize(ds);
        dialog->setFaceType(KPageDialog::List);
        dialog->addPage(new GeneralPage(parentWidget()), i18n("General"), "preferences-system", i18n("General"));
        dialog->addPage(new PatternsPage(Settings::self(), dialog, parentWidget()), i18n("Patterns"), "template",
                        i18n("Patterns"));
        dialog->addPage(new GeneratorPage(m_cellsX - 1, m_cellsY - 1, parentWidget()), i18n("Generator"),
                        "start-here-symbolic", i18n("Generator"));
        dialog->setModal(true);
        connect(dialog, &KConfigDialog::settingsChanged, this, &KLGameField::cdApply);
    }
    dialog->show();


}

void KLGameField::tryLoadFromFile(const QString &path) {
    clearSelection();
    QFileInfo fileInfo(path);
    if (fileInfo.suffix().toLower() == "rle") {
        tryToImportRLE(path);
    } else if (fileInfo.suffix().toLower() == "cells") {
        tryToImportCells(path);
    } else if (fileInfo.suffix().toLower() == "ovf") {
        tryToImportOvf(path);
    } else {
        tryToImportNative(path);
    }

}

void KLGameField::tryToImportOvf(const QString &path) {

    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            throw LoadGameException(i18n("Open file failed").toStdString());
        }

        initLayers();
        QStringList strBuffer;
        QTextStream in(&file);

        while (!in.atEnd()) {

            static QPoint bp(0, 0);
            const QString &strContent = in.readLine();
            if (strContent.startsWith("##ONAME ")) {
                const QStringList &objNames = strContent.split(" ");
                const QString &objName = objNames[1];
                
            } else if (strContent.startsWith("##BPOINT ")) {
                QRegExp re(R"(X([0-9]+)Y([0-9]+))");
                const QStringList &objNames = strContent.split(" ");
                const QString &toSearch = objNames[1];
                if (re.indexIn(toSearch) != -1) {
                    int baseX = re.cap(1).toInt();
                    int baseY = re.cap(2).toInt();
                    bp.setX(baseX);
                    bp.setY(baseY);
                }

            } else {
                QRegExp re(R"(X([0-9]+)Y([0-9]+)L([0-9]+))");
                if (re.indexIn(strContent) != -1) {
                    int baseX = re.cap(1).toInt();
                    int baseY = re.cap(2).toInt();
                    int len = re.cap(3).toInt();
                    for(int i = 0; i < len; i++) {
                        const QPoint &resP = bp + QPoint(baseX, baseY);
                        copyToLayer(m_MainLayer, resP.x() + i, resP.y(), 1);
                    }
                }
            }
        }

        file.close();

    } catch (const LoadGameException &ex) {
        KMessageBox::error(this, QString::fromStdString(ex.what()), i18n("Error"));
    }

}

void KLGameField::tryToImportRLE(const QString &path) {

    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            throw LoadGameException(i18n("Open file failed").toStdString());
        }

        initLayers();
        QStringList strBuffer;
        QTextStream in(&file);

        int origX = 0;
        int origY = 0;
        int deltaX = 0;
        int deltaY = 0;
        while (!in.atEnd()) {

            const QString &strContent = in.readLine();
            if (strContent.startsWith('#')) {
                if (strContent[1].toLatin1() == 'P') {
                    QRegExp re(R"(\s*([0-9]+)\s+([0-9]+))");
                    if (re.indexIn(strContent) != -1) {
                        origX = re.cap(1).toInt() - 1;
                        origY = re.cap(2).toInt() - 1;
                    }
                } else {
                    continue;
                }
            } else if (strContent.toLower().startsWith('x')) {
                QRegExp re(R"(\s*x\s*=\s*([0-9]+)[^y]+y\s*=\s*([0-9]+))");

                if (re.indexIn(strContent) != -1) {
                    deltaX = re.cap(1).toInt();
                    deltaY = re.cap(2).toInt();
                }
            } else {
                strBuffer.append(strContent);
            }
        }
        file.close();

        // In this case deltaX == 0 = fuckup, since
        // x = <int>, y = <int>, rule=B3/S23 should always present in this format
        if (!origX) {
            if (deltaX < m_cellsX) {
                origX = (m_ScrCellsX - (deltaX ? deltaX : (m_ScrCellsX / 2))) / 2;
            }
        }


        const QString &resultContent = strBuffer.join("");
        const QStringList &resContentList = resultContent.split('$');

        // In this case deltaY == 0 = fuckup, since
        // x = <int>, y = <int>, rule=B3/S23 should always present in this format
        if (!origY) {
            if (deltaY < m_cellsY) {
                origY = (m_ScrCellsY - (deltaY ? deltaY : resContentList.count())) / 2;
            }
        }

        int dy = origY;
        for (QString item: resContentList) {
            if (item.endsWith('!')) {
                item.chop(1);
            }
            int dx = origX;

            QRegExp re(R"(([0-9]*)([bo]{1}))");

            int lastPos = 0;
            while ((lastPos = re.indexIn(item, lastPos)) != -1) {

                lastPos += re.matchedLength();

                int quantity = 1;
                if (!re.cap(1).isEmpty()) {
                    quantity = re.cap(1).toInt();
                }
                const QString &symbol = re.cap(2);
                for (int j = 1; j <= quantity; j++) {

                    if (dx < 0 || dx >= m_cellsX || dy < 0 || dy >= m_cellsY) {
                        continue;
                    }

                    if (symbol == "o") {
                        copyToLayer(m_MainLayer, dx, dy, 1);
                    } else if (symbol == "b") {
                        copyToLayer(m_MainLayer, dx, dy, 0);
                    }
                    dx++;
                }

            }
            dy++;
        }

    } catch (const LoadGameException &ex) {
        KMessageBox::error(this, QString::fromStdString(ex.what()), i18n("Error"));
    }

}

void KLGameField::tryToImportCells(const QString &path) {
    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            throw LoadGameException(i18n("Open file failed").toStdString());
        }

        initLayers();
        QStringList strBuffer;
        QTextStream in(&file);

        while (!in.atEnd()) {

            QString strContent = in.readLine();
            if (strContent[0] == '!') {
                continue;
            } else if (strContent[0].toLatin1() == '.' || strContent[0].toLatin1() == 'O') {
                strBuffer.append(strContent);
            }
        }
        file.close();

        int origX = (m_ScrCellsX - strBuffer[0].length()) / 2;
        int origY = (m_ScrCellsY - strBuffer.count()) / 2;
        int dy = origY;
        for (QString &strContent: strBuffer) {
            int dx = origX;
            for (int i = 0; i < strContent.length(); i++) {
                const QCharRef &ch = strContent[i];

                switch (ch.toLatin1()) {
                    case '.':
                        break;
                    case 'O':
                        if (dx < 0 || dx >= m_cellsX || dy < 0 || dy >= m_cellsY) {
                            continue;
                        }

                        copyToLayer(m_MainLayer, dx, dy, 1);
                }
                dx++;
            }
            dy++;
        }


    } catch (const LoadGameException &ex) {
        KMessageBox::error(this, QString::fromStdString(ex.what()), i18n("Error"));
    }
}

void KLGameField::tryToImportNative(const QString &path) {
    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            throw LoadGameException(i18n("Open file failed").toStdString());
        }

        const QByteArray &header = file.read(8);
        const QString &strhead = QString::fromLatin1(header);
        if (strhead != "KGOL_SIG") {
            file.close();
            throw LoadGameException(i18n("Invalid file format").toStdString());
        }


        QByteArray content;
        while (!file.atEnd()) {
            content = file.readAll();
        }
        file.close();
        const QString &strContent = QString::fromLatin1(content);
        const QStringList &coords = strContent.split("CX");
        if (coords.empty()) {
            file.close();
            throw LoadGameException(i18n("Invalid file format").toStdString());
        }

        initLayers();

        for (const QString &item: coords) {
            if (item.isEmpty()) {
                continue;
            } else if (!item.contains('Y')) {
                file.close();
                throw LoadGameException(i18n("Invalid file format").toStdString());
            }

            const QStringList &pairXY = item.split('Y');
            bool ready = false;
            int resX;
            int resY = pairXY[1].toInt(&ready);
            if (ready) {
                resX = pairXY[0].toInt(&ready);
            }

            if (!ready) {
                file.close();
                throw LoadGameException(i18n("Invalid file format").toStdString());
            }

            if (resX < 0 || resX >= m_cellsX || resY < 0 || resY >= m_cellsY) {
                continue;
            }


            copyToLayer(m_MainLayer, resX, resY, 1);
        }

        file.close();

    } catch (const LoadGameException &ex) {
        KMessageBox::error(this, QString::fromStdString(ex.what()), i18n("Error"));
    }
}

void KLGameField::cdApply(const QString &dname) {

    if (!dname.isEmpty()) {
        bool isInfinite = Settings::infinite();
        if (m_isInfinite != isInfinite) {

            clearSelection();
            restoreScreen();

            int oldCellsX = m_cellsX;
            int oldCellsY = m_cellsX;

            int oldSize = oldCellsX * oldCellsY;
            auto *tmpLayer = new uchar[oldSize];
            memcpy(tmpLayer, m_MainLayer, oldSize);

            reallocAllLayers();
            initTotalCells();
            recalcScreenCells();

            for (int y = 0; y < std::min(oldCellsY, m_cellsY); ++y) {
                for (int x = 0; x < std::min(oldCellsX, m_cellsX); ++x) {
                    m_MainLayer[y * m_cellsX + x] = tmpLayer[y * oldCellsX + x];
                }
            }


            delete[] tmpLayer;
        }
    }


    static QString sPath = "";
    const QString &tPath = Settings::templatefile();
    if (!tPath.isEmpty() && tPath != sPath) {
        sPath = tPath;
        tryLoadFromFile(tPath);
        restoreScreen();
    } else {
        repaint();
    }
}

void KLGameField::printGame() {

    cancelTimerInstantly();
    QPrinter printer(QPrinter::HighResolution);

    std::unique_ptr<QPrintDialog> dialog = std::make_unique<QPrintDialog>(&printer, parentWidget());
    dialog->setWindowTitle(i18n("Print Pattern"));


    if (dialog->exec() == QDialog::Accepted) {

        const QRect &destRect = printer.pageLayout().paintRectPixels(printer.resolution());
        QPixmap pixmap = this->grab().scaled(destRect.size());
        QPainter painter;

        painter.begin(&printer);
        painter.drawImage(0, 0, pixmap.toImage());
        painter.end();
    }
}

bool KLGameField::doMiniMaxTestsOnLayer(uchar *layer, QPoint &minOr, QPoint &maxOr) {
    bool isToSave = false;
    for (int y = 0; y < m_cellsY; ++y) {
        for (int x = 0; x < m_cellsX; ++x) {
            if (fromLayer(layer, x, y)) {
                isToSave = true;
                minOr.setX(std::min(x, minOr.x()));
                minOr.setY(std::min(y, minOr.y()));

                maxOr.setX(std::max(x, maxOr.x()));
                maxOr.setY(std::max(y, maxOr.y()));
            }
        }
    }
    return isToSave;
}


void KLGameField::tryToExportFromLayerNative(uchar *layer, const QString &path) {
    try {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            throw LoadGameException(i18n("Open file failed").toStdString());
        }

        bool isToSave = false;
        QByteArray ba;

        QTextStream tmp(&ba);

        for (int y = 0; y < m_cellsY; ++y) {
            for (int x = 0; x < m_cellsX; ++x) {
                if (fromLayer(layer, x, y)) {
                    isToSave = true;
                    tmp << "CX" << x << "Y" << y;
                }
            }
        }

        if (!isToSave) {
            throw LoadGameException(i18n("Colony is empty").toStdString());
        }

        tmp.flush();
        QTextStream out(&file);
        out << "KGOL_SIG";
        out << ba;
        file.close();
    } catch (const LoadGameException &ex) {
        KMessageBox::error(this, QString::fromStdString(ex.what()), i18n("Error"));
    }
}

void KLGameField::tryToExportFromLayerCells(uchar *layer, const QString &path) {
    bool isToSave;
    try {

        QPoint minOrigin = QPoint(m_cellsX, m_cellsY);
        QPoint maxOrigin = QPoint(0, 0);

        QSize delta = QSize(0, 0);

        // First we perform minimax tests to determine pattern limits;

        isToSave = doMiniMaxTestsOnLayer(layer, minOrigin, maxOrigin);

        if (!isToSave) {
            throw LoadGameException(i18n("Colony is empty").toStdString());
        }

        delta.setWidth(maxOrigin.x() - minOrigin.x());
        delta.setHeight(maxOrigin.y() - minOrigin.y());

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            throw LoadGameException(i18n("Open file failed").toStdString());
        }

        QTextStream out(&file);
        out << "! X-Creator: KGLife\n";

        for (int y = 0; y <= delta.height(); ++y) {
            for (int x = 0; x <= delta.width(); ++x) {
                out << (fromLayer(layer, minOrigin.x() + x, minOrigin.y() + y) ? 'O' : '.');
            }
            out << '\n';
        }

        file.close();

    } catch (const LoadGameException &ex) {
        KMessageBox::error(this, QString::fromStdString(ex.what()), i18n("Error"));
    }
}

void KLGameField::tryToExportFromLayerRLE(uchar *layer, const QString &path) {

    bool isToSave;
    try {

        QPoint minOrigin = QPoint(m_cellsX, m_cellsY);
        QPoint maxOrigin = QPoint(0, 0);

        QSize delta = QSize(0, 0);

        // First we perform minimax tests to determine pattern limits;

        isToSave = doMiniMaxTestsOnLayer(layer, minOrigin, maxOrigin);

        if (!isToSave) {
            throw LoadGameException(i18n("Colony is empty").toStdString());
        }

        delta.setWidth(maxOrigin.x() - minOrigin.x());
        delta.setHeight(maxOrigin.y() - minOrigin.y());

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            throw LoadGameException(i18n("Open file failed").toStdString());
        }

        QTextStream out(&file);
        out << "#C X-Creator: KGLife\n";
        out << "#P " << minOrigin.x() + 1 << " " << minOrigin.y() + 1 << '\n';
        out << "x = " << delta.width() + 1 << ", y = " << delta.height() + 1 << ", rule=B3/S23\n";

        for (int y = 0; y <= delta.height(); ++y) {
            int cDead = 0;
            int cAlive = 0;
            for (int x = 0; x <= delta.width(); ++x) {
                if (fromLayer(layer, minOrigin.x() + x, minOrigin.y() + y)) {
                    cAlive++;
                    flushStream(cDead, 'b', out);
                } else {
                    cDead++;
                    flushStream(cAlive, 'o', out);
                }
            }
            flushStream(cAlive, 'o', out);
            out << (y == delta.height() ? '!' : '$');
        }

        file.close();

    } catch (const LoadGameException &ex) {
        KMessageBox::error(this, QString::fromStdString(ex.what()), i18n("Error"));
    }
}

void KLGameField::flushStream(int &status, char symbol, QTextStream &s) {

    if (status) {
        if (status > 1) {
            s << status;
        }
        s << symbol;
        status = 0;
    }
}

void KLGameField::generatorReady(int rx, int ry) {

    if (rx >= 0 && rx < m_cellsX && ry >= 0 && ry < m_cellsY && !fromMainLayer(rx, ry)) {
        copyToLayer(m_MainLayer, rx, ry, 1);
    }
}

void KLGameField::startCellsGenerator(bool) {

    clearSelection();
    if (Settings::initbeforegenerate()) {
        initLayers();
        restoreScreen();
    }
    int totalQuantity = m_ScrCellsX * m_ScrCellsY;
    int percentToFill = Settings::fillpercentvalue();
    int readyQuantity = totalQuantity / 100 * percentToFill;

    // actual max values: m_ScrCellsX  - 1 and m_ScrCellsY - 1
    mGenerator->init(QSize(m_ScrCellsX - 1, m_ScrCellsY - 1), readyQuantity);
    mGenerator->start(QThread::LowPriority);
}

void KLGameField::initLayers() {
    m_SelectionLayer = initLayer(m_SelectionLayer);
    m_MainLayer = initLayer(m_MainLayer);
    m_NextStepLayer = initLayer(m_NextStepLayer);
}

void KLGameField::clearSelection() {
    memset(m_SelectionLayer, 0, m_cellsX * m_cellsY);
    repaint();
}

void KLGameField::onSelectClear(bool) {
    clearSelection();
}

QList<GLifeObject> &KLGameField::buildChordesGroups() {

    static QList<GLifeObject> objList;
    objList.clear();
    QPoint minOrigin = QPoint(m_cellsX, m_cellsY);
    QPoint maxOrigin = QPoint(0, 0);
    QSize delta = QSize(0, 0);
    bool isToAnalyze = doMiniMaxTestsOnLayer(m_MainLayer, minOrigin, maxOrigin);
    if (isToAnalyze) {
        delta.setWidth(maxOrigin.x() - minOrigin.x());
        delta.setHeight(maxOrigin.y() - minOrigin.y());

        for (int y = 0; y <= delta.height(); ++y) {
            int x = 0;
            do {
                while (x <= delta.width() && !fromLayer(m_MainLayer, minOrigin.x() + x, minOrigin.y() + y)) {
                    x++;
                }
                int len = 0;
                int bx = x;
                while (fromLayer(m_MainLayer, minOrigin.x() + x, minOrigin.y() + y)) {
                    x++;
                    len++;
                }

                if (len) {
                    const QPoint &absPoint = minOrigin + QPoint(bx, y);
                    QVector<int> currentChorde = {absPoint.x(), absPoint.y(), len};
                    bool added = false;
                    if (!objList.isEmpty())  {
                        for (GLifeObject &lifeObj : objList) {
                            if (lifeObj.hasLinkedForChorde(currentChorde)) {
                                lifeObj.add(currentChorde);
                                added = true;
                                break;
                            }
                        }
                    }

                    if (!added) {
                        GLifeObject obj(minOrigin, i18n("Unknown Object"), nullptr);
                        obj.add(currentChorde);
                        objList.append(obj);
                    }
                }
            } while(x <= delta.width());

        }

    }

    return objList;
    
}

void KLGameField::analizeObjects(QList<GLifeObject> &l) const {

    for (GLifeObject &o : l) {
        const QList<QVector<int>> &vectors = o.listChordes();
        if (vectors.length() == 2) {
            if (vectors[0][2] == 2 && vectors[0][2] == vectors[1][2]) {
                if(vectors[0][0] == vectors[1][0]) {
                    o.setlifeObjectName(i18n("Square"));
                } else {
                    o.setlifeObjectName(i18n("Zigzag"));
                }
                continue;
            }
        }
        if (vectors.length() == 3) {
            if (vectors[1][0] - vectors[0][0] == 1 &&
                vectors[0][2] == vectors[1][2] &&
                vectors[1][2] == 1 &&
                vectors[2][2] == 3 &&
                vectors[1][0] - vectors[2][0] == 2) {
                o.setlifeObjectName(i18n("Glider"));
                continue;
            }
            if (vectors[0][0] == vectors[1][0] &&
                vectors[1][0] == vectors[2][0] &&
                vectors[0][2] == vectors[1][2] &&
                vectors[1][2] == vectors[2][2] &&
                vectors[2][2] == 1) {
                o.setlifeObjectName(i18n("Vertical Blinker"));
                continue;
            }
        }
        if(vectors.length() == 1 && vectors[0][2] == 3) {
            o.setlifeObjectName(i18n("Horizontal Blinker"));
        }
    }
}

void KLGameField::reselectObject(const GLifeObject &chosenObj) {
    clearSelection();
    for (const QVector<int> &vect : chosenObj.listChordes()) {
        int cellX = vect[0];
        int cellY = vect[1];
        int len = vect[2];
        for(int i = 0; i < len; i++) {
            copyToLayer(m_SelectionLayer, cellX + i, cellY, 1);
        }
    }
    repaint();
}

void KLGameField::chordesAction(bool) {

    QList<GLifeObject> &objList = buildChordesGroups();
    if (!objList.empty()) {
        analizeObjects(objList);
        QStringList qsl;
        for (const GLifeObject &o : objList) {
            qsl << o.getlifeObjectName();
        }
        auto *d = new AnalysysDialog(qsl, this);

        connect(d->lifeObjects, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int index) {
            const GLifeObject &chosenObj = objList[index];
            reselectObject(chosenObj);
        });
        d->setWindowTitle(i18n("Result"));
        reselectObject(objList[0]);
        int retCode = d->exec();
        clearSelection();
        delete d;
        if (retCode == QDialog::Accepted) {
            QString filters = i18n("Object Vector Format (*.ovf)");
            const QString &path = QFileDialog::getSaveFileName(parentWidget(), i18n("Save objects model"),
                                                        CurrentFilePath.isEmpty() ? QDir::homePath() :
                                                        QFileInfo(CurrentFilePath).dir().path(),
                                                        filters, &filters);

            if (!path.isEmpty()) {
                try {
                    QFile file(path);
                    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                        throw LoadGameException(i18n("Open file failed").toStdString());
                    }

                    QTextStream out(&file);
                    out << "MODEL_SIG" << '\n';

                    for (const GLifeObject &obj : objList) {
                        out << "##ONAME " << obj.getlifeObjectName() << '\n';
                        const QPoint &absol = obj.getAbsol();
                        out << "##BPOINT X" << absol.x() << "Y" << absol.y() << '\n';
                        for (const QVector<int> &vec : obj.listChordes()) {
                            QPoint bp(vec[0], vec[1]);
                            bp -= absol;
                            out << "X" << bp.x() << "Y" << bp.y() << "L" << vec[2] << '\n';
                        }
                    }

                    file.close();
                } catch (const LoadGameException &ex) {
                    KMessageBox::error(this, QString::fromStdString(ex.what()), i18n("Error"));
                }
            }

        }
    }
}

void KLGameField::showContextMenu(const QPoint &pos) {
    QPoint newPos = pos;
    if (!checkMousePosition(newPos)) {
        return;
    }
    cancelTimerInstantly();

    if (popupMenu.isNull()) {
        return;
    }

    m_LeftbPressed = false;

    QPoint minOrigin = QPoint(m_cellsX, m_cellsY);
    QPoint maxOrigin = QPoint(0, 0);

    bool isToCopy = doMiniMaxTestsOnLayer(m_SelectionLayer, minOrigin, maxOrigin);
    QAction *res = actionCollection()->action(QStringLiteral("paste_selected"));
    res->setEnabled(isToCopy);

    actionCollection()->action(QStringLiteral("fill_selected"))->setEnabled(isToCopy);
    actionCollection()->action(QStringLiteral("empty_selected"))->setEnabled(isToCopy);
    actionCollection()->action(QStringLiteral("select_untouched"))->setEnabled(isToCopy);
    actionCollection()->action(QStringLiteral("save_selection"))->setEnabled(isToCopy);

    if (isToCopy) {
        const QMetaObject::Connection &popupConn = connect(res, &QAction::triggered, this, [=](bool) {
            int cellX = newPos.x() / (m_cellSize + SPACE);
            int cellY = newPos.y() / (m_cellSize + SPACE);

            if (!maxOrigin.isNull()) {
                QSize delta(0, 0);
                delta.setWidth(maxOrigin.x() - minOrigin.x());
                delta.setHeight(maxOrigin.y() - minOrigin.y());
                for (int y = 0; y <= delta.height(); ++y) {
                    for (int x = 0; x <= delta.width(); ++x) {
                        copyToLayer(m_MainLayer, m_CurrMemOffsetX + cellX + x, m_CurrMemOffsetY + cellY + y,
                                    fromLayer(m_SelectionLayer, minOrigin.x() + x, minOrigin.y() + y));
                    }
                }
                repaint();
            }
        });
        popupMenu->exec(mapToGlobal(pos));
        disconnect(popupConn);
    } else {
        popupMenu->exec(mapToGlobal(pos));
    }
}

void KLGameField::setupCommonActions() {
    setXMLFile(QStringLiteral("actionsui.rc"));

    QAction *pasteAction = actionCollection()->addAction(QStringLiteral("paste_selected"));
    pasteAction->setText(i18n("Paste at this position"));
    pasteAction->setIcon(QIcon::fromTheme("edit-paste-symbolic"));
    pasteAction->setObjectName("paste_selected");

    QAction *emptyAction = actionCollection()->addAction(QStringLiteral("empty_selected"), this,
                                                         &KLGameField::fillOrEmptySelected);
    emptyAction->setText(i18n("Empty"));
    emptyAction->setIcon(QIcon::fromTheme("trash-empty"));
    emptyAction->setObjectName("empty_selected");
    actionCollection()->setDefaultShortcut(emptyAction, Qt::ALT + Qt::Key_E);

    QAction *fillAction = actionCollection()->addAction(QStringLiteral("fill_selected"), this,
                                                        &KLGameField::fillOrEmptySelected);
    fillAction->setText(i18n("Fill"));
    fillAction->setIcon(QIcon::fromTheme("fill-color"));
    fillAction->setObjectName("fill_selected");
    actionCollection()->setDefaultShortcut(fillAction, Qt::ALT + Qt::Key_F);

    QAction *untouchedAction = actionCollection()->addAction(QStringLiteral("select_untouched"), this,
                                                             &KLGameField::fillOrEmptySelected);
    untouchedAction->setText(i18n("Select untouched"));
    untouchedAction->setIcon(QIcon::fromTheme("filled-xterm"));
    untouchedAction->setObjectName("select_untouched");
    actionCollection()->setDefaultShortcut(untouchedAction, Qt::ALT + Qt::Key_U);

    QAction *saveAction = actionCollection()->addAction(QStringLiteral("save_selection"), this,
                                                        &KLGameField::fillOrEmptySelected);
    saveAction->setText(i18n("Save selected"));
    saveAction->setIcon(QIcon::fromTheme("document-save"));
    saveAction->setObjectName("save_selection");

}

void KLGameField::fillOrEmptySelected(bool) {

    QPoint minOrigin = QPoint(m_cellsX, m_cellsY);
    QPoint maxOrigin = QPoint(0, 0);

    doMiniMaxTestsOnLayer(m_SelectionLayer, minOrigin, maxOrigin);

    if (!maxOrigin.isNull()) {

        bool toEmpty = (sender()->objectName() == "empty_selected");
        bool toFill = (sender()->objectName() == "fill_selected");
        bool untouched = (sender()->objectName() == "select_untouched");
        bool toSave = (sender()->objectName() == "save_selection");

        if (toSave) {
            const QString &path = forceFileNameDialog();

            if (path.isEmpty()) {
                return;
            }
            trySaveToFileLayer(m_SelectionLayer, path);
            return;
        }

        for (int y = minOrigin.y(); y <= maxOrigin.y(); ++y) {
            for (int x = minOrigin.x(); x <= maxOrigin.x(); ++x) {
                if (toEmpty || toFill) {
                    copyToLayer(m_MainLayer, x, y, toEmpty ? 0 : 1);
                } else if (untouched) {
                    copyToLayer(m_SelectionLayer, x, y, fromLayer(m_MainLayer, x, y));
                }
            }
        }
        if (toEmpty) {
            clearSelection();
        }

        repaint();
    }
}

void KLGameField::setupFactory() {
    if (!factory()) {
        if (clientBuilder() == nullptr) {
            // Client builder does not get deleted automatically, we handle this
            _clientBuilder = new KXMLGUIBuilder(this);
            setClientBuilder(_clientBuilder);
        }

        auto factory = new KXMLGUIFactory(clientBuilder());
        factory->addClient(this);
    }

    popupMenu = qobject_cast<QMenu *>(factory()->container(QStringLiteral("actions-popup-menu"), this));
}

int KLGameField::getCellsX() {
    return m_cellsX;
}

int KLGameField::getCellsY() {
    return m_cellsY;
}

uchar *KLGameField::getMainLayer() {
    return m_MainLayer;
}




























