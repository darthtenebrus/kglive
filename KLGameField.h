//
// Created by esorochinskiy on 26.09.23.
//

#ifndef KGLIVE_KLGAMEFIELD_H
#define KGLIVE_KLGAMEFIELD_H


#include <QWidget>
#include <QTextStream>
#include <KXMLGUIClient>
#include <KXMLGUIBuilder>
#include <QPointer>
#include <QMenu>
#include "cellsgenerator.h"
#include "GLifeObject.h"

class KLGameField : public QWidget, public KXMLGUIClient {
Q_OBJECT
public:
    KLGameField(int, QWidget * = nullptr);
    ~KLGameField() override;
    void cancelTimerInstantly(void);

    int getCellsX();
    int getCellsY();
    uchar *getMainLayer();
    bool doMiniMaxTestsOnLayer(uchar *layer, QPoint &minOr, QPoint &maxOr);


protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void recalcScreenCells(void);

    static int sgn(int val);
    void intentToMoveField(int x, int y);

private:
    void setupCommonActions();
    void setupFactory();
    void initLayers();
    void clearSelection();
    void reallocAllLayers();
    void initTotalCells();
    void changeDelta(int);
    void restoreScreen();

    void recalculate();
    void nextAction();
    void actualDoRePaint();
    uchar *initLayer(uchar *) const;
    void swapLayers(void);
    int calculateNeighbors(int, int);

    uchar fromMainLayer(int, int);
    uchar fromLayer(uchar *, int, int) const;

    void copyToLayer(uchar *, int, int, uchar) const;
    bool checkMousePosition(QPoint &) const;

    void tryLoadFromFile(const QString &);
    void tryToImportNative(const QString &);
    void tryToImportCells(const QString &);
    void tryToImportRLE(const QString &);
    void tryToImportOvf(const QString &qString);

    void trySaveToFile(const QString &);
    void trySaveToFileLayer(uchar *, const QString &);


    void tryToExportFromLayerNative(uchar *, const QString &);
    void tryToExportFromLayerRLE(uchar *, const QString &);
    void tryToExportFromLayerCells(uchar *, const QString &);


    QString forceFileNameDialog();


    [[nodiscard]]
    QPoint getMainOffset() const;

    [[nodiscard]]
    static QSize getStandardFieldDefs(int &, int &) ;

    static void flushStream(int &status, char symbol, QTextStream &);

    QList<GLifeObject> &buildChordesGroups();
    void analizeObjects(QList<GLifeObject> &) const;

    void reselectObject(const GLifeObject &);

    int m_TimerInterval;
    int m_Generation = 0;

    int m_cellsX = 0;
    int m_remScrX;
    int m_cellsY = 0;
    int m_remScrY;
    int m_MaxMemOffsetX = 0;
    int m_MaxMemOffsetY = 0;

    int m_CurrMemOffsetX = 0;
    int m_CurrMemOffsetY = 0;


    int m_cellSize;
    int m_ScrCellsX;
    int m_ScrCellsY;

    CellsGenerator *mGenerator = nullptr;

    uchar *m_MainLayer = nullptr;
    uchar *m_NextStepLayer = nullptr;
    uchar *m_SelectionLayer = nullptr;

    QTimer *evoTimer = nullptr;
    QCursor m_Cursor;
    bool m_LeftbPressed = false;
    bool m_MoveMode = false;
    bool m_SelectionMode = false;
    bool m_isInfinite = false;

    KXMLGUIBuilder *_clientBuilder;
    QPointer<QMenu> popupMenu;


    static QString CurrentFilePath;
    static void setCurrentPath(KLGameField *th, const QString& cpath);


public slots:
    void newAction(bool);
    void singleAction(bool);
    void openAction(bool);
    void saveAction(bool);
    void saveAsAction(bool);
    void checkTimerAndUpdate(bool);
    void timerChanged(int);

    void setupGame();
    void printGame();
    void changeMoveMode(bool);
    void changeSelectMode(bool);

    void cZoomIn(bool);
    void cZoomOut(bool);
    void cRestore(bool);
    void cdApply(const QString &);
    void generatorReady(int, int);
    void startCellsGenerator(bool);
    void onSelectClear(bool);

    void chordesAction(bool);

private slots:
    void nextGeneration();
    void showContextMenu(const QPoint &);
    void fillOrEmptySelected(bool);


signals:
    void changeControls(bool);
    void changeGeneration(int);
    void emptyColony(void);

    void changeZoomIn(bool);
    void changeZoomOut(bool);
    void changeRestore(bool);
    void changeCurrentFile(const QString &);

};


#endif //KGLIVE_KLGAMEFIELD_H
