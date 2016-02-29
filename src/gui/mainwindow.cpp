/*****************************************************************************
* Copyright 2015-2016 Alexander Barthel albar965@mailbox.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "gui/dialog.h"
#include "gui/errorhandler.h"
#include "sql/sqldatabase.h"
#include "settings/settings.h"
#include "logging/loggingdefs.h"
#include "logging/logginghandler.h"
#include "gui/translator.h"
#include "fs/fspaths.h"
#include "table/search.h"
#include "map/navmapwidget.h"
#include <marble/MarbleModel.h>
#include <marble/GeoDataPlacemark.h>
#include <marble/GeoDataDocument.h>
#include <marble/GeoDataTreeModel.h>
#include <marble/MarbleWidgetPopupMenu.h>
#include <marble/MarbleWidgetInputHandler.h>
#include <marble/GeoDataStyle.h>
#include <marble/GeoDataIconStyle.h>
#include <marble/RenderPlugin.h>
#include <marble/MarbleDirs.h>
#include <marble/QtMarbleConfigDialog.h>

#include <QCloseEvent>
#include <QProgressDialog>
#include <QSettings>

#include "settings/settings.h"
#include "fs/bglreaderoptions.h"
#include "table/controller.h"
#include "gui/widgetstate.h"
#include "gui/tablezoomhandler.h"
#include "fs/bglreaderprogressinfo.h"
#include "fs/navdatabase.h"
#include "table/searchcontroller.h"
#include "gui/helphandler.h"

#include <sql/sqlutil.h>

using namespace Marble;
using atools::settings::Settings;

MainWindow::MainWindow(QWidget *parent) :
  QMainWindow(parent), ui(new Ui::MainWindow)
{
  qDebug() << "MainWindow constructor";

  QString aboutMessage =
    tr("<p>is a fast flight planner and airport search tool for FSX.</p>"
         "<p>This software is licensed under "
           "<a href=\"http://www.gnu.org/licenses/gpl-3.0\">GPL3</a> or any later version.</p>"
             "<p>The source code for this application is available at "
               "<a href=\"https://github.com/albar965\">Github</a>.</p>"
                 "<p><b>Copyright 2015-2016 Alexander Barthel (albar965@mailbox.org).</b></p>");

  dialog = new atools::gui::Dialog(this);
  errorHandler = new atools::gui::ErrorHandler(this);
  helpHandler = new atools::gui::HelpHandler(this, aboutMessage, GIT_REVISION);

  ui->setupUi(this);
  setupUi();

  readSettings();
  openDatabase();

  createNavMap();

  searchPanes = new SearchController(this, &db);
  searchPanes->createAirportSearch(ui->tableViewAirportSearch);
  searchPanes->createNavSearch(ui->tableViewNavSearch);

  searchPanes->restoreState();
  mapWidget->restoreState();

  connectAllSlots();
  updateActionStates();

#if 0
  GeoDataDocument *document = new GeoDataDocument;

  GeoDataIconStyle *style = new GeoDataIconStyle;
  // style->setIconPath(":/littlenavmap/resources/icons/checkmark.svg");
  GeoDataStyle *style2 = new GeoDataStyle;
  style2->setIconStyle(*style);

  atools::sql::SqlQuery query(db);

  query.exec("select ident, name, lonx, laty, altitude from airport where rating > 0");

  while(query.next())
  {
    GeoDataPlacemark *place = new GeoDataPlacemark(query.value("ident").toString() + " " +
                                                   query.value("name").toString());
    place->setCoordinate(query.value("lonx").toDouble(), query.value("laty").toDouble(),
                         query.value("altitude").toDouble(), GeoDataCoordinates::Degree);
    // place->setDescription("Test place");
    // place->setCountryCode("Germany");
    // place->setStyle(style2);

    document->append(place);
  }

  // Add the document to MarbleWidget's tree model
  mapWidget->model()->treeModel()->addDocument(document);
#endif
}

void MainWindow::createNavMap()
{
  MarbleDirs::setMarbleDataPath(QApplication::applicationDirPath() + QDir::separator() + "data");
  MarbleDirs::setMarblePluginPath(QApplication::applicationDirPath() + QDir::separator() + "plugins");

  qDebug() << "Marble Local Path:" << MarbleDirs::localPath();
  qDebug() << "Marble Plugin Local Path:" << MarbleDirs::pluginLocalPath();
  qDebug() << "Marble Data Path (Run Time) :" << MarbleDirs::marbleDataPath();
  qDebug() << "Marble Plugin Path (Run Time) :" << MarbleDirs::marblePluginPath();
  qDebug() << "Marble System Path:" << MarbleDirs::systemPath();
  qDebug() << "Marble Plugin System Path:" << MarbleDirs::pluginSystemPath();

  // Create a Marble QWidget without a parent
  mapWidget = new NavMapWidget(this);

  // Load the OpenStreetMap map
  mapWidget->setMapThemeId("earth/openstreetmap/openstreetmap.dgml");
  // mapWidget->setMapThemeId("earth/bluemarble/bluemarble.dgml");
  // mapWidget->setShowBorders( true );
  // mapWidget->setShowClouds( true );
  // mapWidget->setProjection( Marble::Mercator );
  // mapWidget->setAnimationsEnabled(false);
  mapWidget->setShowCrosshairs(false);
  mapWidget->setShowBackground(false);
  mapWidget->setShowAtmosphere(false);
  mapWidget->setShowGrid(true);
  // mapWidget->setShowTerrain(true);place marks
  mapWidget->setShowRelief(true);
  mapWidget->setVolatileTileCacheLimit(512 * 1024);

  // mapWidget->setShowSunShading(true);

  // mapWidget->model()->addGeoDataFile("/home/alex/ownCloud/Flight Simulator/FSX/Airports KML/NA Blue.kml");
  // mapWidget->model()->addGeoDataFile( "/home/alex/Downloads/map.osm" );

  MarbleWidgetInputHandler *inputHandler = mapWidget->inputHandler();
  inputHandler->setMouseButtonPopupEnabled(Qt::RightButton, false);
  inputHandler->setMouseButtonPopupEnabled(Qt::LeftButton, false);
  mapWidget->setContextMenuPolicy(Qt::CustomContextMenu);

  ui->verticalLayout_10->replaceWidget(ui->mapWidgetDummy, mapWidget);

  QSet<QString> pluginEnable;
  pluginEnable << "Compass" << "Coordinate Grid" << "License" << "Scale Bar" << "Navigation"
               << "Overview Map" << "Position Marker" << "Elevation Profile" << "Elevation Profile Marker"
               << "Download Progress Indicator";

  // pluginDisable << "Annotation" << "Amateur Radio Aprs Plugin" << "Atmosphere" << "Compass" <<
  // "Crosshairs" << "Earthquakes" << "Eclipses" << "Elevation Profile" << "Elevation Profile Marker" <<
  // "Places" << "GpsInfo" << "Coordinate Grid" << "License" << "Scale Bar" << "Measure Tool" << "Navigation" <<
  // "OpenCaching.Com" << "OpenDesktop Items" << "Overview Map" << "Photos" << "Position Marker" <<
  // "Postal Codes" << "Download Progress Indicator" << "Routing" << "Satellites" << "Speedometer" << "Stars" <<
  // "Sun" << "Weather" << "Wikipedia Articles";

  QList<RenderPlugin *> localRenderPlugins = mapWidget->renderPlugins();
  for(RenderPlugin *p : localRenderPlugins)
    if(!pluginEnable.contains(p->name()))
    {
      qDebug() << "Disabled plugin" << p->name();
      p->setEnabled(false);
    }
    else
      qDebug() << "Found plugin" << p->name();

  // MarbleWidgetPopupMenu *menu = mapWidget->popupMenu();
  // QAction tst(QString("Menu"), mapWidget);
  // menu->addAction(Qt::RightButton, &tst);
}

MainWindow::~MainWindow()
{
  delete searchPanes;
  delete ui;

  qDebug() << "MainWindow destructor";

  delete dialog;
  delete errorHandler;
  delete progressDialog;

  atools::settings::Settings::shutdown();
  atools::gui::Translator::unload();

  closeDatabase();

  qDebug() << "MainWindow destructor about to shut down logging";
  atools::logging::LoggingHandler::shutdown();

}

void MainWindow::setupUi()
{
  ui->menuView->addAction(ui->mainToolBar->toggleViewAction());
  ui->menuView->addAction(ui->dockWidgetSearch->toggleViewAction());
  ui->menuView->addAction(ui->dockWidgetRoute->toggleViewAction());
  ui->menuView->addAction(ui->dockWidgetAirportInfo->toggleViewAction());
}

void MainWindow::createEmptySchema()
{
  if(!atools::sql::SqlUtil(&db).hasTable("airport"))
  {
    atools::fs::BglReaderOptions opts;
    atools::fs::Navdatabase nd(&opts, &db);
    nd.createSchema();
  }
}

void MainWindow::loadScenery()
{
  preDatabaseLoad();
  using atools::fs::BglReaderOptions;
  QString config = Settings::getOverloadedPath(":/littlenavmap/resources/config/navdatareader.cfg");
  QSettings settings(config, QSettings::IniFormat);

  BglReaderOptions opts;
  opts.loadFromSettings(settings);

  progressDialog = new QProgressDialog(this);
  QLabel *label = new QLabel(progressDialog);
  label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  label->setIndent(10);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setMinimumWidth(640);

  progressDialog->setWindowModality(Qt::WindowModal);
  progressDialog->setLabel(label);
  progressDialog->setAutoClose(false);
  progressDialog->setAutoReset(false);
  progressDialog->setMinimumDuration(0);
  progressDialog->show();

  atools::fs::fstype::SimulatorType type = atools::fs::fstype::FSX;
  QString sceneryFile = atools::fs::FsPaths::getSceneryLibraryPath(type);
  QString basepath = atools::fs::FsPaths::getBasePath(type);

  opts.setSceneryFile(sceneryFile);
  opts.setBasepath(basepath);

  opts.setProgressCallback(std::bind(&MainWindow::progressCallback, this, std::placeholders::_1));

  // Let the dialog close and show the busy pointer
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

  atools::fs::Navdatabase nd(&opts, &db);
  nd.create();

  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  if(!progressDialog->wasCanceled())
  {
    progressDialog->setCancelButtonText(tr("&OK"));
    progressDialog->exec();
  }
  delete progressDialog;
  progressDialog = nullptr;

  postDatabaseLoad(false);
}

bool MainWindow::progressCallback(const atools::fs::BglReaderProgressInfo& progress)
{
  if(progress.isFirstCall())
  {
    progressDialog->setMinimum(0);
    progressDialog->setMaximum(progress.getTotal());
  }
  progressDialog->setValue(progress.getCurrent());

  static const QString text(tr("<b>%1</b><br/><br/><br/>"
                               "<b>Files:</b> %L2<br/>"
                               "<b>Airports:</b> %L3<br/>"
                               "<b>VOR:</b> %L4<br/>"
                               "<b>ILS:</b> %L5<br/>"
                               "<b>NDB:</b> %L6<br/>"
                               "<b>Marker:</b> %L7<br/>"
                               "<b>Boundaries:</b> %L8<br/>"
                               "<b>Waypoints:</b> %L9"));

  static const QString textWithFile(tr("<b>Scenery:</b> %1 (%2).<br/>"
                                       "<b>File:</b> %3.<br/><br/>"
                                       "<b>Files:</b> %L4<br/>"
                                       "<b>Airports:</b> %L5<br/>"
                                       "<b>VOR:</b> %L6<br/>"
                                       "<b>ILS:</b> %L7<br/>"
                                       "<b>NDB:</b> %L8<br/>"
                                       "<b>Marker:</b> %L9<br/>"
                                       "<b>Boundaries:</b> %L10<br/>"
                                       "<b>Waypoints:</b> %L11"));

  if(progress.isNewOther())
    progressDialog->setLabelText(
      text.arg(progress.getOtherAction()).
      arg(progress.getNumFiles()).
      arg(progress.getNumAirports()).
      arg(progress.getNumVors()).
      arg(progress.getNumIls()).
      arg(progress.getNumNdbs()).
      arg(progress.getNumMarker()).
      arg(progress.getNumBoundaries()).
      arg(progress.getNumWaypoints()));
  else if(progress.isNewSceneryArea() || progress.isNewFile())
    progressDialog->setLabelText(
      textWithFile.arg(progress.getSceneryTitle()).
      arg(progress.getSceneryPath()).
      arg(progress.getBglFilename()).
      arg(progress.getNumFiles()).
      arg(progress.getNumAirports()).
      arg(progress.getNumVors()).
      arg(progress.getNumIls()).
      arg(progress.getNumNdbs()).
      arg(progress.getNumMarker()).
      arg(progress.getNumBoundaries()).
      arg(progress.getNumWaypoints()));
  else if(progress.isLastCall())
    progressDialog->setLabelText(
      text.arg(tr("Done")).
      arg(progress.getNumFiles()).
      arg(progress.getNumAirports()).
      arg(progress.getNumVors()).
      arg(progress.getNumIls()).
      arg(progress.getNumNdbs()).
      arg(progress.getNumMarker()).
      arg(progress.getNumBoundaries()).
      arg(progress.getNumWaypoints()));

  return progressDialog->wasCanceled();
}

void MainWindow::connectAllSlots()
{
  qDebug() << "Connecting slots";

  connect(searchPanes->getAirportSearch(), &Search::showPoint, mapWidget, &NavMapWidget::showPoint);
  connect(searchPanes->getNavSearch(), &Search::showPoint, mapWidget, &NavMapWidget::showPoint);

  // Use this event to show path dialog after main windows is shown
  connect(this, &MainWindow::windowShown, this, &MainWindow::mainWindowShown, Qt::QueuedConnection);

  connect(ui->actionShowStatusbar, &QAction::toggled, ui->statusBar, &QStatusBar::setVisible);
  connect(ui->actionExit, &QAction::triggered, this, &MainWindow::close);
  connect(ui->actionReloadScenery, &QAction::triggered, this, &MainWindow::loadScenery);
  connect(ui->actionOptions, &QAction::triggered, this, &MainWindow::options);

  connect(ui->actionContents, &QAction::triggered, helpHandler, &atools::gui::HelpHandler::help);
  connect(ui->actionAbout, &QAction::triggered, helpHandler, &atools::gui::HelpHandler::about);
  connect(ui->actionAbout_Qt, &QAction::triggered, helpHandler, &atools::gui::HelpHandler::aboutQt);
}

void MainWindow::options()
{
  // QtMarbleConfigDialog dlg(mapWidget);
  // dlg.exec();

}

void MainWindow::mainWindowShown()
{
  qDebug() << "MainWindow::mainWindowShown()";
}

void MainWindow::updateActionStates()
{
  qDebug() << "Updating action states";
  ui->actionShowStatusbar->setChecked(!ui->statusBar->isHidden());
}

void MainWindow::openDatabase()
{
  try
  {
    using atools::sql::SqlDatabase;

    // Get a file in the organization specific directory with an application
    // specific name (i.e. Linux: ~/.config/ABarthel/little_logbook.sqlite)
    databaseFile = atools::settings::Settings::getConfigFilename(".sqlite");

    qDebug() << "Opening database" << databaseFile;
    db = SqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(databaseFile);
    db.open({"PRAGMA foreign_keys = ON"});

    createEmptySchema();
  }
  catch(std::exception& e)
  {
    errorHandler->handleException(e, "While opening database");
  }
  catch(...)
  {
    errorHandler->handleUnknownException("While opening database");
  }
}

void MainWindow::closeDatabase()
{
  try
  {
    using atools::sql::SqlDatabase;

    qDebug() << "Closing database" << databaseFile;
    db.close();
  }
  catch(std::exception& e)
  {
    errorHandler->handleException(e, "While closing database");
  }
  catch(...)
  {
    errorHandler->handleUnknownException("While closing database");
  }
}

void MainWindow::readSettings()
{
  qDebug() << "readSettings";

  atools::gui::WidgetState ws("MainWindow/Widget");
  ws.restore({this, ui->statusBar, ui->tabWidgetSearch});
}

void MainWindow::writeSettings()
{
  qDebug() << "writeSettings";

  atools::gui::WidgetState ws("MainWindow/Widget");
  ws.save({this, ui->statusBar, ui->tabWidgetSearch});
  ws.syncSettings();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  // Catch all close events like Ctrl-Q or Menu/Exit or clicking on the
  // close button on the window frame
  qDebug() << "closeEvent";
  int result = dialog->showQuestionMsgBox("Actions/ShowQuit",
                                          tr("Really Quit?"),
                                          tr("Do not &show this dialog again."),
                                          QMessageBox::Yes | QMessageBox::No,
                                          QMessageBox::No, QMessageBox::Yes);

  if(result != QMessageBox::Yes)
    event->ignore();
  writeSettings();
  searchPanes->saveState();
  mapWidget->saveState();
}

void MainWindow::showEvent(QShowEvent *event)
{
  if(firstStart)
  {
    emit windowShown();
    firstStart = false;
  }

  event->ignore();
}

void MainWindow::preDatabaseLoad()
{
  if(!hasDatabaseLoadStatus)
  {
    hasDatabaseLoadStatus = true;
    searchPanes->preDatabaseLoad();
  }
  else
    qDebug() << "Already in database loading status";
}

void MainWindow::postDatabaseLoad(bool force)
{
  if(hasDatabaseLoadStatus || force)
  {
    searchPanes->postDatabaseLoad();
    hasDatabaseLoadStatus = false;
  }
  else
    qDebug() << "Not in database loading status";
}
