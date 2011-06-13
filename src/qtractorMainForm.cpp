// qtractorMainForm.cpp
//
/****************************************************************************
   Copyright (C) 2005-2011, rncbc aka Rui Nuno Capela. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*****************************************************************************/

#include "qtractorMainForm.h"

#include "qtractorAbout.h"
#include "qtractorOptions.h"
#include "qtractorInstrument.h"
#include "qtractorMessages.h"
#include "qtractorFiles.h"
#include "qtractorConnections.h"
#include "qtractorMixer.h"

#include "qtractorTracks.h"
#include "qtractorTrackList.h"
#include "qtractorTrackTime.h"
#include "qtractorTrackView.h"
#include "qtractorThumbView.h"
#include "qtractorSpinBox.h"

#include "qtractorAudioPeak.h"
#include "qtractorAudioBuffer.h"
#include "qtractorAudioEngine.h"
#include "qtractorMidiEngine.h"

#include "qtractorSessionDocument.h"
#include "qtractorSessionCursor.h"

#include "qtractorSessionCommand.h"
#include "qtractorTimeScaleCommand.h"
#include "qtractorClipCommand.h"

#include "qtractorAudioClip.h"
#include "qtractorMidiClip.h"

#include "qtractorAudioMeter.h"
#include "qtractorMidiMeter.h"

#include "qtractorMidiMonitor.h"
#include "qtractorMidiBuffer.h"

#include "qtractorExportForm.h"
#include "qtractorSessionForm.h"
#include "qtractorOptionsForm.h"
#include "qtractorConnectForm.h"
#include "qtractorShortcutForm.h"
#include "qtractorMidiControlForm.h"
#include "qtractorInstrumentForm.h"
#include "qtractorBusForm.h"
#include "qtractorTimeScaleForm.h"
#include "qtractorTempoAdjustForm.h"

#include "qtractorMidiEditorForm.h"
#include "qtractorMidiEditor.h"

#include "qtractorTrackCommand.h"

#include "qtractorOscControl.h"

#include "qtractorCurve.h"

#ifdef CONFIG_DSSI
#include "qtractorDssiPlugin.h"
#endif

#ifdef CONFIG_VST
#include "qtractorVstPlugin.h"
#endif

#ifdef CONFIG_LV2
#include "qtractorLv2Plugin.h"
#endif

#ifdef CONFIG_JACK_SESSION
#include <jack/session.h>
#endif

#include <QApplication>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QRegExp>
#include <QUrl>

#include <QDomDocument>

#include <QSocketNotifier>
#include <QActionGroup>
#include <QStatusBar>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QTimer>
#include <QDateTime>
#include <QClipboard>
#include <QProgressBar>

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QCloseEvent>
#include <QDropEvent>

#if !defined(QT_NO_STYLE_GTK)
#include <QGtkStyle>
#endif

#if QT_VERSION < 0x040500
namespace Qt {
const WindowFlags WindowCloseButtonHint = WindowFlags(0x08000000);
#if QT_VERSION < 0x040200
const WindowFlags CustomizeWindowHint   = WindowFlags(0x02000000);
#endif
}
#endif


#if defined(WIN32)
#undef HAVE_SIGNAL_H
#endif

//-------------------------------------------------------------------------
// LADISH Level 1 support stuff.

#ifdef HAVE_SIGNAL_H

#include <QSocketNotifier>

#include <sys/types.h>
#include <sys/socket.h>

#include <signal.h>

// File descriptor for SIGUSR1 notifier.
static int g_fdUsr1[2];

// Unix SIGUSR1 signal handler.
static void qtractor_sigusr1_handler ( int /* signo */ )
{
	char c = 1;

	(::write(g_fdUsr1[0], &c, sizeof(c)) > 0);
}

#endif	// HANDLE_SIGNAL_H


#include <math.h>


// Timer constant (magic) stuff.
#define QTRACTOR_TIMER_MSECS    66
#define QTRACTOR_TIMER_DELAY    233


//-------------------------------------------------------------------------
// qtractorTempoCursor -- Custom session tempo helper class

class qtractorTempoCursor
{
public:

	// Constructor.
	qtractorTempoCursor() : m_pNode(NULL) {}

	// Reset method.
	void clear() { m_pNode = NULL; }

	// Predicate method.
	qtractorTimeScale::Node *seek(
		qtractorSession *pSession, unsigned long iFrame)
	{
		qtractorTimeScale::Cursor& cursor = pSession->timeScale()->cursor();
		qtractorTimeScale::Node *pNode = cursor.seekFrame(iFrame);
		return (m_pNode == pNode ? NULL : m_pNode = pNode);
	}

private:

	// Instance variables.
	qtractorTimeScale::Node *m_pNode;
};


//-------------------------------------------------------------------------
// qtractorMainForm -- Main window form implementation.

// Kind of singleton reference.
qtractorMainForm *qtractorMainForm::g_pMainForm = NULL;

// Constructor.
qtractorMainForm::qtractorMainForm (
	QWidget *pParent, Qt::WindowFlags wflags )
	: QMainWindow(pParent, wflags)
{
	// Setup UI struct...
	m_ui.setupUi(this);

	// Pseudo-singleton reference setup.
	g_pMainForm = this;

	// Initialize some pointer references.
	m_pOptions = NULL;

	// FIXME: This gotta go, somwhere in time...
	m_pSession = qtractorSession::getInstance();
	m_pTempoCursor = new qtractorTempoCursor();

	// All child forms are to be created later, not earlier than setup.
	m_pMessages    = NULL;
	m_pFiles       = NULL;
	m_pMixer       = NULL;
	m_pConnections = NULL;
	m_pTracks      = NULL;

	// To remember last time we've shown the playhead.
	m_iPlayHead = 0;

	// We'll start clean.
	m_iUntitled   = 0;
	m_iDirtyCount = 0;

	m_iPeakTimer = 0;
	m_iPlayTimer = 0;
	m_iIdleTimer = 0;

	m_iTransportUpdate  = 0; 
	m_iTransportRolling = 0;
	m_bTransportPlaying = false;
	m_fTransportShuttle = 0.0f;
	m_iTransportStep    = 0;

	m_iXrunCount = 0;
	m_iXrunSkip  = 0;
	m_iXrunTimer = 0;

	m_iAudioRefreshTimer = 0;
	m_iMidiRefreshTimer  = 0;

	m_iPlayerTimer = 0;

	// Configure the audio file peak factory...
	if (m_pSession->audioPeakFactory()) {
		QObject::connect(m_pSession->audioPeakFactory(),
			SIGNAL(peakEvent()),
			SLOT(peakNotify()));
	}

	// Configure the audio engine event handling...
	qtractorAudioEngine *pAudioEngine = m_pSession->audioEngine();
	if (pAudioEngine) {
		QObject::connect(pAudioEngine->proxy(),
			SIGNAL(shutEvent()),
			SLOT(audioShutNotify()));
		QObject::connect(pAudioEngine->proxy(),
			SIGNAL(xrunEvent()),
			SLOT(audioXrunNotify()));
		QObject::connect(pAudioEngine->proxy(),
			SIGNAL(portEvent()),
			SLOT(audioPortNotify()));
		QObject::connect(pAudioEngine->proxy(),
			SIGNAL(buffEvent()),
			SLOT(audioBuffNotify()));
		QObject::connect(pAudioEngine->proxy(),
			SIGNAL(sessEvent(void *)),
			SLOT(audioSessNotify(void *)));
		QObject::connect(pAudioEngine->proxy(),
			SIGNAL(syncEvent(unsigned long)),
			SLOT(audioSyncNotify(unsigned long)));
	}

	// Configure the MIDI engine event handling...
	qtractorMidiEngine *pMidiEngine = m_pSession->midiEngine();
	if (pMidiEngine) {
		qRegisterMetaType<qtractorMmcEvent> ("qtractorMmcEvent");
		qRegisterMetaType<qtractorCtlEvent> ("qtractorCtlEvent");
		QObject::connect(pMidiEngine->proxy(),
			SIGNAL(mmcEvent(const qtractorMmcEvent&)),
			SLOT(midiMmcNotify(const qtractorMmcEvent&)));
		QObject::connect(pMidiEngine->proxy(),
			SIGNAL(ctlEvent(const qtractorCtlEvent&)),
			SLOT(midiCtlNotify(const qtractorCtlEvent&)));
		QObject::connect(pMidiEngine->proxy(),
			SIGNAL(sppEvent(int, unsigned short)),
			SLOT(midiSppNotify(int, unsigned short)));
		QObject::connect(pMidiEngine->proxy(),
			SIGNAL(clkEvent(float)),
			SLOT(midiClkNotify(float)));
	}

	// Add the midi controller map...
	m_pMidiControl = new qtractorMidiControl();

	// Also the OSC control server (TESTING)...
	m_pOscControl = new qtractorOscControl();

#ifdef HAVE_SIGNAL_H

	// Set to ignore any fatal "Broken pipe" signals.
	::signal(SIGPIPE, SIG_IGN);

	// LADISH Level 1 suport.
	// Initialize file descriptors for SIGUSR1 socket notifier.
	::socketpair(AF_UNIX, SOCK_STREAM, 0, g_fdUsr1);
	m_pUsr1Notifier
		= new QSocketNotifier(g_fdUsr1[1], QSocketNotifier::Read, this);

	QObject::connect(m_pUsr1Notifier,
		SIGNAL(activated(int)),
		SLOT(handle_sigusr1()));

	// Install SIGUSR1 signal handler.
    struct sigaction usr1;
    usr1.sa_handler = qtractor_sigusr1_handler;
    ::sigemptyset(&usr1.sa_mask);
    usr1.sa_flags = 0;
    usr1.sa_flags |= SA_RESTART;
    ::sigaction(SIGUSR1, &usr1, NULL);

#else	// HAVE_SIGNAL_H

	m_pUsr1Notifier = NULL;
	
#endif	// !HAVE_SIGNAL_H

	// Get edit selection mode action group up...
//	m_ui.editToolbar->addSeparator();
	m_pSelectModeActionGroup = new QActionGroup(this);
	m_pSelectModeActionGroup->setExclusive(true);
//	m_pSelectModeActionGroup->setUsesDropDown(true);
	m_pSelectModeActionGroup->addAction(m_ui.editSelectModeClipAction);
	m_pSelectModeActionGroup->addAction(m_ui.editSelectModeRangeAction);
	m_pSelectModeActionGroup->addAction(m_ui.editSelectModeRectAction);
//	m_ui.editToolbar->addActions(m_pSelectModeActionGroup->actions());

	// Additional time-toolbar controls...
//	m_ui.timeToolbar->addSeparator();

	// Editable toolbar widgets special palette.
	QPalette pal;
	// Outrageous HACK: GTK+ ppl won't see green on black thing...
#if !defined(QT_NO_STYLE_GTK)
	if (qobject_cast<QGtkStyle *> (style()) == NULL) {
#endif
	//	pal.setColor(QPalette::Window, Qt::black);
		pal.setColor(QPalette::Base, Qt::black);
		pal.setColor(QPalette::Text, Qt::green);
	//	pal.setColor(QPalette::Button, Qt::darkGray);
	//	pal.setColor(QPalette::ButtonText, Qt::green);
#if !defined(QT_NO_STYLE_GTK)
	}
#endif

	const QSize  pad(4, 0);
	const QFont& font0 = qtractorMainForm::font();
	const QFont  font(font0.family(), font0.pointSize() + 2);
	const QFontMetrics fm(font);
	const int d = fm.height() + fm.leading() + 8;
	
	// Transport time.
	const QString sTime("+99:99:99.999");
	m_pTimeSpinBox = new qtractorTimeSpinBox();
	m_pTimeSpinBox->setTimeScale(m_pSession->timeScale());
	m_pTimeSpinBox->setFont(font);
	m_pTimeSpinBox->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	m_pTimeSpinBox->setMinimumSize(QSize(fm.width(sTime) + d, d) + pad);
	m_pTimeSpinBox->setPalette(pal);
//	m_pTimeSpinBox->setAutoFillBackground(true);
	m_pTimeSpinBox->setToolTip(tr("Current time (playhead)"));
	m_pTimeSpinBox->setContextMenuPolicy(Qt::CustomContextMenu);
	m_ui.timeToolbar->addWidget(m_pTimeSpinBox);
//	m_ui.timeToolbar->addSeparator();

	// Tempo spin-box.
	const QString sTempo("999.9 9/9");
	m_pTempoSpinBox = new qtractorTempoSpinBox();
//	m_pTempoSpinBox->setDecimals(1);
//	m_pTempoSpinBox->setMinimum(1.0f);
//	m_pTempoSpinBox->setMaximum(1000.0f);
	m_pTempoSpinBox->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
	m_pTempoSpinBox->setMinimumSize(QSize(fm.width(sTempo) + d, d) + pad);
	m_pTempoSpinBox->setPalette(pal);
//	m_pTempoSpinBox->setAutoFillBackground(true);
	m_pTempoSpinBox->setToolTip(tr("Current tempo (BPM)"));
	m_pTempoSpinBox->setContextMenuPolicy(Qt::CustomContextMenu);
	m_ui.timeToolbar->addWidget(m_pTempoSpinBox);
	m_ui.timeToolbar->addSeparator();

	// Snap-per-beat combo-box.
	m_pSnapPerBeatComboBox = new QComboBox();
	m_pSnapPerBeatComboBox->setEditable(false);
	m_pSnapPerBeatComboBox->insertItems(0, qtractorTimeScale::snapItems());
	m_pSnapPerBeatComboBox->setToolTip(tr("Snap/beat"));
	m_ui.timeToolbar->addWidget(m_pSnapPerBeatComboBox);

	// Track-line thumbnail view...
	m_pThumbView = new qtractorThumbView();
	m_ui.thumbToolbar->addWidget(m_pThumbView);
	m_ui.thumbToolbar->setAllowedAreas(
		Qt::TopToolBarArea | Qt::BottomToolBarArea);

	QObject::connect(m_pTimeSpinBox,
		SIGNAL(customContextMenuRequested(const QPoint&)),
		SLOT(transportTimeContextMenu(const QPoint&)));
	QObject::connect(m_pTimeSpinBox,
		SIGNAL(valueChanged(unsigned long)),
		SLOT(transportTimeChanged(unsigned long)));
	QObject::connect(m_pTimeSpinBox,
		SIGNAL(editingFinished()),
		SLOT(transportTimeFinished()));
	QObject::connect(m_pTempoSpinBox,
		SIGNAL(customContextMenuRequested(const QPoint&)),
		SLOT(transportTempoContextMenu(const QPoint&)));
	QObject::connect(m_pTempoSpinBox,
		SIGNAL(valueChanged(float, unsigned short, unsigned short)),
		SLOT(transportTempoChanged(float, unsigned short, unsigned short)));
	QObject::connect(m_pTempoSpinBox,
		SIGNAL(editingFinished()),
		SLOT(transportTempoFinished()));
	QObject::connect(m_pSnapPerBeatComboBox,
		SIGNAL(activated(int)),
		SLOT(snapPerBeatChanged(int)));

	// Create some statusbar labels...
	QStatusBar *pStatusBar = statusBar();

	QLabel *pLabel;
	QPalette *pPalette = new QPalette(statusBar()->palette());
	m_paletteItems[PaletteNone] = pPalette;

	pPalette = new QPalette(pStatusBar->palette());
	pPalette->setColor(QPalette::Window, Qt::red);
	m_paletteItems[PaletteRed] = pPalette;

	pPalette = new QPalette(pStatusBar->palette());
	pPalette->setColor(QPalette::Window, Qt::yellow);
	m_paletteItems[PaletteYellow] = pPalette;

	pPalette = new QPalette(pStatusBar->palette());
	pPalette->setColor(QPalette::Window, Qt::cyan);
	m_paletteItems[PaletteCyan] = pPalette;

	pPalette = new QPalette(pStatusBar->palette());
	pPalette->setColor(QPalette::Window, Qt::green);
	m_paletteItems[PaletteGreen] = pPalette;

	// Track status.
	pLabel = new QLabel(tr("Track"));
	pLabel->setAlignment(Qt::AlignLeft);
	pLabel->setToolTip(tr("Current track name"));
	pLabel->setAutoFillBackground(true);
	m_statusItems[StatusName] = pLabel;
	pStatusBar->addWidget(pLabel, 2);

	// Hideous progress bar...
	m_pProgressBar = new QProgressBar();
	m_pProgressBar->setFixedHeight(pLabel->sizeHint().height());
	m_pProgressBar->setMinimumWidth(120);
	pStatusBar->addPermanentWidget(m_pProgressBar);
	m_pProgressBar->hide();

	// Session modification status.
	pLabel = new QLabel(tr("MOD"));
	pLabel->setAlignment(Qt::AlignHCenter);
	pLabel->setMinimumSize(pLabel->sizeHint() + pad);
	pLabel->setToolTip(tr("Session modification state"));
	pLabel->setAutoFillBackground(true);
	m_statusItems[StatusMod] = pLabel;
	pStatusBar->addPermanentWidget(pLabel);

	// Session recording status.
	pLabel = new QLabel(tr("REC"));
	pLabel->setAlignment(Qt::AlignHCenter);
	pLabel->setMinimumSize(pLabel->sizeHint() + pad);
	pLabel->setToolTip(tr("Session record state"));
	pLabel->setAutoFillBackground(true);
	m_statusItems[StatusRec] = pLabel;
	pStatusBar->addPermanentWidget(pLabel);

	// Session muting status.
	pLabel = new QLabel(tr("MUTE"));
	pLabel->setAlignment(Qt::AlignHCenter);
	pLabel->setMinimumSize(pLabel->sizeHint() + pad);
	pLabel->setToolTip(tr("Session muting state"));
	pLabel->setAutoFillBackground(true);
	m_statusItems[StatusMute] = pLabel;
	pStatusBar->addPermanentWidget(pLabel);

	// Session soloing status.
	pLabel = new QLabel(tr("SOLO"));
	pLabel->setAlignment(Qt::AlignHCenter);
	pLabel->setMinimumSize(pLabel->sizeHint() + pad);
	pLabel->setToolTip(tr("Session soloing state"));
	pLabel->setAutoFillBackground(true);
	m_statusItems[StatusSolo] = pLabel;
	pStatusBar->addPermanentWidget(pLabel);

	// Session looping status.
	pLabel = new QLabel(tr("LOOP"));
	pLabel->setAlignment(Qt::AlignHCenter);
	pLabel->setMinimumSize(pLabel->sizeHint() + pad);
	pLabel->setToolTip(tr("Session looping state"));
	pLabel->setAutoFillBackground(true);
	m_statusItems[StatusLoop] = pLabel;
	pStatusBar->addPermanentWidget(pLabel);

	// Session length time.
	pLabel = new QLabel(sTime);
	pLabel->setAlignment(Qt::AlignHCenter);
	pLabel->setMinimumSize(pLabel->sizeHint() + pad);
	pLabel->setToolTip(tr("Session total time"));
	pLabel->setAutoFillBackground(true);
	m_statusItems[StatusTime] = pLabel;
	pStatusBar->addPermanentWidget(pLabel);

	// Session sample rate.
	pLabel = new QLabel("199999 Hz");
	pLabel->setAlignment(Qt::AlignHCenter);
	pLabel->setMinimumSize(pLabel->sizeHint() + pad);
	pLabel->setAutoFillBackground(true);
	pLabel->setToolTip(tr("Session sample rate"));
	m_statusItems[StatusRate] = pLabel;
	pStatusBar->addPermanentWidget(pLabel);

#if QT_VERSION >= 0x040200
	m_ui.transportLoopAction->setAutoRepeat(false);
	m_ui.transportLoopSetAction->setAutoRepeat(false);
	m_ui.transportPlayAction->setAutoRepeat(false);
	m_ui.transportRecordAction->setAutoRepeat(false);
	m_ui.transportPunchAction->setAutoRepeat(false);
	m_ui.transportPunchSetAction->setAutoRepeat(false);
#endif

	// Some actions surely need those
	// shortcuts firmly attached...
	addAction(m_ui.viewMenubarAction);
#if 0
	const QList<QAction *>& actions = findChildren<QAction *> ();
	QListIterator<QAction *> iter(actions);
	while (iter.hasNext())
		iter.next()->setShortcutContext(Qt::ApplicationShortcut);
#else
	m_ui.viewFilesAction->setShortcutContext(Qt::ApplicationShortcut);
	m_ui.viewConnectionsAction->setShortcutContext(Qt::ApplicationShortcut);
	m_ui.viewMixerAction->setShortcutContext(Qt::ApplicationShortcut);
	m_ui.viewMessagesAction->setShortcutContext(Qt::ApplicationShortcut);
#endif
	// Ah, make it stand right.
	setFocus();

	// UI signal/slot connections...
	QObject::connect(m_ui.fileNewAction,
		SIGNAL(triggered(bool)),
		SLOT(fileNew()));
	QObject::connect(m_ui.fileOpenAction,
		SIGNAL(triggered(bool)),
		SLOT(fileOpen()));
	QObject::connect(m_ui.fileSaveAction,
		SIGNAL(triggered(bool)),
		SLOT(fileSave()));
	QObject::connect(m_ui.fileSaveAsAction,
		SIGNAL(triggered(bool)),
		SLOT(fileSaveAs()));
	QObject::connect(m_ui.filePropertiesAction,
		SIGNAL(triggered(bool)),
		SLOT(fileProperties()));
	QObject::connect(m_ui.fileExitAction,
		SIGNAL(triggered(bool)),
		SLOT(fileExit()));

	QObject::connect(m_ui.editUndoAction,
		SIGNAL(triggered(bool)),
		SLOT(editUndo()));
	QObject::connect(m_ui.editRedoAction,
		SIGNAL(triggered(bool)),
		SLOT(editRedo()));
	QObject::connect(m_ui.editCutAction,
		SIGNAL(triggered(bool)),
		SLOT(editCut()));
	QObject::connect(m_ui.editCopyAction,
		SIGNAL(triggered(bool)),
		SLOT(editCopy()));
	QObject::connect(m_ui.editPasteAction,
		SIGNAL(triggered(bool)),
		SLOT(editPaste()));
	QObject::connect(m_ui.editPasteRepeatAction,
		SIGNAL(triggered(bool)),
		SLOT(editPasteRepeat()));
	QObject::connect(m_ui.editDeleteAction,
		SIGNAL(triggered(bool)),
		SLOT(editDelete()));
	QObject::connect(m_ui.editSelectModeClipAction,
		SIGNAL(triggered(bool)),
		SLOT(editSelectModeClip()));
	QObject::connect(m_ui.editSelectModeRangeAction,
		SIGNAL(triggered(bool)),
		SLOT(editSelectModeRange()));
	QObject::connect(m_ui.editSelectModeRectAction,
		SIGNAL(triggered(bool)),
		SLOT(editSelectModeRect()));
	QObject::connect(m_ui.editSelectAllAction,
		SIGNAL(triggered(bool)),
		SLOT(editSelectAll()));
	QObject::connect(m_ui.editSelectNoneAction,
		SIGNAL(triggered(bool)),
		SLOT(editSelectNone()));
	QObject::connect(m_ui.editSelectInvertAction,
		SIGNAL(triggered(bool)),
		SLOT(editSelectInvert()));
	QObject::connect(m_ui.editSelectTrackAction,
		SIGNAL(triggered(bool)),
		SLOT(editSelectTrack()));
	QObject::connect(m_ui.editSelectTrackRangeAction,
		SIGNAL(triggered(bool)),
		SLOT(editSelectTrackRange()));
	QObject::connect(m_ui.editSelectRangeAction,
		SIGNAL(triggered(bool)),
		SLOT(editSelectRange()));
	QObject::connect(m_ui.editClipNewAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipNew()));
	QObject::connect(m_ui.editClipEditAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipEdit()));
	QObject::connect(m_ui.editClipSplitAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipSplit()));
	QObject::connect(m_ui.editClipMergeAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipMerge()));
	QObject::connect(m_ui.editClipNormalizeAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipNormalize()));
	QObject::connect(m_ui.editClipToolsQuantizeAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipToolsQuantize()));
	QObject::connect(m_ui.editClipToolsTransposeAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipToolsTranspose()));
	QObject::connect(m_ui.editClipToolsNormalizeAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipToolsNormalize()));
	QObject::connect(m_ui.editClipToolsRandomizeAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipToolsRandomize()));
	QObject::connect(m_ui.editClipToolsResizeAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipToolsResize()));
	QObject::connect(m_ui.editClipToolsRescaleAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipToolsRescale()));
	QObject::connect(m_ui.editClipToolsTimeshiftAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipToolsTimeshift()));
	QObject::connect(m_ui.editClipTempoAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipTempo()));
	QObject::connect(m_ui.editClipImportAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipImport()));
	QObject::connect(m_ui.editClipExportAction,
		SIGNAL(triggered(bool)),
		SLOT(editClipExport()));

	QObject::connect(m_ui.trackAddAction,
		SIGNAL(triggered(bool)),
		SLOT(trackAdd()));
	QObject::connect(m_ui.trackRemoveAction,
		SIGNAL(triggered(bool)),
		SLOT(trackRemove()));
	QObject::connect(m_ui.trackPropertiesAction,
		SIGNAL(triggered(bool)),
		SLOT(trackProperties()));
	QObject::connect(m_ui.trackInputsAction,
		SIGNAL(triggered(bool)),
		SLOT(trackInputs()));
	QObject::connect(m_ui.trackOutputsAction,
		SIGNAL(triggered(bool)),
		SLOT(trackOutputs()));
	QObject::connect(m_ui.trackStateRecordAction,
		SIGNAL(triggered(bool)),
		SLOT(trackStateRecord(bool)));
	QObject::connect(m_ui.trackStateMuteAction,
		SIGNAL(triggered(bool)),
		SLOT(trackStateMute(bool)));
	QObject::connect(m_ui.trackStateSoloAction,
		SIGNAL(triggered(bool)),
		SLOT(trackStateSolo(bool)));
	QObject::connect(m_ui.trackStateMonitorAction,
		SIGNAL(triggered(bool)),
		SLOT(trackStateMonitor(bool)));
	QObject::connect(m_ui.trackNavigateFirstAction,
		SIGNAL(triggered(bool)),
		SLOT(trackNavigateFirst()));
	QObject::connect(m_ui.trackNavigatePrevAction,
		SIGNAL(triggered(bool)),
		SLOT(trackNavigatePrev()));
	QObject::connect(m_ui.trackNavigateNextAction,
		SIGNAL(triggered(bool)),
		SLOT(trackNavigateNext()));
	QObject::connect(m_ui.trackNavigateLastAction,
		SIGNAL(triggered(bool)),
		SLOT(trackNavigateLast()));
	QObject::connect(m_ui.trackNavigateNoneAction,
		SIGNAL(triggered(bool)),
		SLOT(trackNavigateNone()));
	QObject::connect(m_ui.trackMoveTopAction,
		SIGNAL(triggered(bool)),
		SLOT(trackMoveTop()));
	QObject::connect(m_ui.trackMoveUpAction,
		SIGNAL(triggered(bool)),
		SLOT(trackMoveUp()));
	QObject::connect(m_ui.trackMoveDownAction,
		SIGNAL(triggered(bool)),
		SLOT(trackMoveDown()));
	QObject::connect(m_ui.trackMoveBottomAction,
		SIGNAL(triggered(bool)),
		SLOT(trackMoveBottom()));
	QObject::connect(m_ui.trackAutoMonitorAction,
		SIGNAL(triggered(bool)),
		SLOT(trackAutoMonitor(bool)));
	QObject::connect(m_ui.trackImportAudioAction,
		SIGNAL(triggered(bool)),
		SLOT(trackImportAudio()));
	QObject::connect(m_ui.trackImportMidiAction,
		SIGNAL(triggered(bool)),
		SLOT(trackImportMidi()));
	QObject::connect(m_ui.trackExportAudioAction,
		SIGNAL(triggered(bool)),
		SLOT(trackExportAudio()));
	QObject::connect(m_ui.trackExportMidiAction,
		SIGNAL(triggered(bool)),
		SLOT(trackExportMidi()));
	QObject::connect(m_ui.trackCurveSelectMenu,
		SIGNAL(triggered(QAction *)),
		SLOT(trackCurveSelect(QAction *)));
	QObject::connect(m_ui.trackCurveModeMenu,
		SIGNAL(triggered(QAction *)),
		SLOT(trackCurveMode(QAction *)));
	QObject::connect(m_ui.trackCurveProcessAction,
		SIGNAL(triggered(bool)),
		SLOT(trackCurveProcess(bool)));
	QObject::connect(m_ui.trackCurveCaptureAction,
		SIGNAL(triggered(bool)),
		SLOT(trackCurveCapture(bool)));
	QObject::connect(m_ui.trackCurveClearAction,
		SIGNAL(triggered(bool)),
		SLOT(trackCurveClear()));
	QObject::connect(m_ui.trackCurveProcessAllAction,
		SIGNAL(triggered(bool)),
		SLOT(trackCurveProcessAll(bool)));
	QObject::connect(m_ui.trackCurveCaptureAllAction,
		SIGNAL(triggered(bool)),
		SLOT(trackCurveCaptureAll(bool)));
	QObject::connect(m_ui.trackCurveClearAllAction,
		SIGNAL(triggered(bool)),
		SLOT(trackCurveClearAll()));

	QObject::connect(m_ui.viewMenubarAction,
		SIGNAL(triggered(bool)),
		SLOT(viewMenubar(bool)));
	QObject::connect(m_ui.viewStatusbarAction,
		SIGNAL(triggered(bool)),
		SLOT(viewStatusbar(bool)));
	QObject::connect(m_ui.viewToolbarFileAction,
		SIGNAL(triggered(bool)),
		SLOT(viewToolbarFile(bool)));
	QObject::connect(m_ui.viewToolbarEditAction,
		SIGNAL(triggered(bool)),
		SLOT(viewToolbarEdit(bool)));
	QObject::connect(m_ui.viewToolbarTrackAction,
		SIGNAL(triggered(bool)),
		SLOT(viewToolbarTrack(bool)));
	QObject::connect(m_ui.viewToolbarViewAction,
		SIGNAL(triggered(bool)),
		SLOT(viewToolbarView(bool)));
	QObject::connect(m_ui.viewToolbarOptionsAction,
		SIGNAL(triggered(bool)),
		SLOT(viewToolbarOptions(bool)));
	QObject::connect(m_ui.viewToolbarTransportAction,
		SIGNAL(triggered(bool)),
		SLOT(viewToolbarTransport(bool)));
	QObject::connect(m_ui.viewToolbarTimeAction,
		SIGNAL(triggered(bool)),
		SLOT(viewToolbarTime(bool)));
	QObject::connect(m_ui.viewToolbarThumbAction,
		SIGNAL(triggered(bool)),
		SLOT(viewToolbarThumb(bool)));
	QObject::connect(m_ui.viewFilesAction,
		SIGNAL(triggered(bool)),
		SLOT(viewFiles(bool)));
	QObject::connect(m_ui.viewMessagesAction,
		SIGNAL(triggered(bool)),
		SLOT(viewMessages(bool)));
	QObject::connect(m_ui.viewConnectionsAction,
		SIGNAL(triggered(bool)),
		SLOT(viewConnections(bool)));
	QObject::connect(m_ui.viewMixerAction,
		SIGNAL(triggered(bool)),
		SLOT(viewMixer(bool)));
	QObject::connect(m_ui.viewZoomInAction,
		SIGNAL(triggered(bool)),
		SLOT(viewZoomIn()));
	QObject::connect(m_ui.viewZoomOutAction,
		SIGNAL(triggered(bool)),
		SLOT(viewZoomOut()));
	QObject::connect(m_ui.viewZoomResetAction,
		SIGNAL(triggered(bool)),
		SLOT(viewZoomReset()));
	QObject::connect(m_ui.viewZoomHorizontalAction,
		SIGNAL(triggered(bool)),
		SLOT(viewZoomHorizontal()));
	QObject::connect(m_ui.viewZoomVerticalAction,
		SIGNAL(triggered(bool)),
		SLOT(viewZoomVertical()));
	QObject::connect(m_ui.viewZoomAllAction,
		SIGNAL(triggered(bool)),
		SLOT(viewZoomAll()));
	QObject::connect(m_ui.viewSnapGridAction,
		SIGNAL(triggered(bool)),
		SLOT(viewSnapGrid(bool)));
	QObject::connect(m_ui.viewToolTipsAction,
		SIGNAL(triggered(bool)),
		SLOT(viewToolTips(bool)));
	QObject::connect(m_ui.viewRefreshAction,
		SIGNAL(triggered(bool)),
		SLOT(viewRefresh()));
	QObject::connect(m_ui.viewInstrumentsAction,
		SIGNAL(triggered(bool)),
		SLOT(viewInstruments()));
	QObject::connect(m_ui.viewControllersAction,
		SIGNAL(triggered(bool)),
		SLOT(viewControllers()));
	QObject::connect(m_ui.viewBusesAction,
		SIGNAL(triggered(bool)),
		SLOT(viewBuses()));
	QObject::connect(m_ui.viewTempoMapAction,
		SIGNAL(triggered(bool)),
		SLOT(viewTempoMap()));
	QObject::connect(m_ui.viewOptionsAction,
		SIGNAL(triggered(bool)),
		SLOT(viewOptions()));

	QObject::connect(m_ui.transportBackwardAction,
		SIGNAL(triggered(bool)),
		SLOT(transportBackward()));
	QObject::connect(m_ui.transportRewindAction,
		SIGNAL(triggered(bool)),
		SLOT(transportRewind()));
	QObject::connect(m_ui.transportFastForwardAction,
		SIGNAL(triggered(bool)),
		SLOT(transportFastForward()));
	QObject::connect(m_ui.transportForwardAction,
		SIGNAL(triggered(bool)),
		SLOT(transportForward()));
	QObject::connect(m_ui.transportLoopAction,
		SIGNAL(triggered(bool)),
		SLOT(transportLoop()));
	QObject::connect(m_ui.transportLoopSetAction,
		SIGNAL(triggered(bool)),
		SLOT(transportLoopSet()));
	QObject::connect(m_ui.transportStopAction,
		SIGNAL(triggered(bool)),
		SLOT(transportStop()));
	QObject::connect(m_ui.transportPlayAction,
		SIGNAL(triggered(bool)),
		SLOT(transportPlay()));
	QObject::connect(m_ui.transportRecordAction,
		SIGNAL(triggered(bool)),
		SLOT(transportRecord()));
	QObject::connect(m_ui.transportPunchAction,
		SIGNAL(triggered(bool)),
		SLOT(transportPunch()));
	QObject::connect(m_ui.transportPunchSetAction,
		SIGNAL(triggered(bool)),
		SLOT(transportPunchSet()));
	QObject::connect(m_ui.transportMetroAction,
		SIGNAL(triggered(bool)),
		SLOT(transportMetro()));
	QObject::connect(m_ui.transportFollowAction,
		SIGNAL(triggered(bool)),
		SLOT(transportFollow()));
	QObject::connect(m_ui.transportAutoBackwardAction,
		SIGNAL(triggered(bool)),
		SLOT(transportAutoBackward()));
	QObject::connect(m_ui.transportContinueAction,
		SIGNAL(triggered(bool)),
		SLOT(transportContinue()));

	QObject::connect(m_ui.helpShortcutsAction,
		SIGNAL(triggered(bool)),
		SLOT(helpShortcuts()));
	QObject::connect(m_ui.helpAboutAction,
		SIGNAL(triggered(bool)),
		SLOT(helpAbout()));
	QObject::connect(m_ui.helpAboutQtAction,
		SIGNAL(triggered(bool)),
		SLOT(helpAboutQt()));

	QObject::connect(m_ui.fileMenu,
		SIGNAL(aboutToShow()),
		SLOT(updateRecentFilesMenu()));
	QObject::connect(m_ui.editClipMenu,
		SIGNAL(aboutToShow()),
		SLOT(updateClipMenu()));
	QObject::connect(m_ui.trackExportMenu,
		SIGNAL(aboutToShow()),
		SLOT(updateExportMenu()));
	QObject::connect(m_ui.trackCurveMenu,
		SIGNAL(aboutToShow()),
		SLOT(updateCurveMenu()));
	QObject::connect(m_ui.viewZoomMenu,
		SIGNAL(aboutToShow()),
		SLOT(updateZoomMenu()));
	QObject::connect(m_ui.viewSnapMenu,
		SIGNAL(aboutToShow()),
		SLOT(updateSnapMenu()));

	QObject::connect(m_pSession->commands(),
		SIGNAL(updateNotifySignal(bool)),
		SLOT(updateNotifySlot(bool)));

//	Already handled in files widget...
//	QObject::connect(QApplication::clipboard(),
//		SIGNAL(dataChanged()),
//		SLOT(stabilizeForm()));
}


// Destructor.
qtractorMainForm::~qtractorMainForm (void)
{
#ifdef HAVE_SIGNAL_H
	if (m_pUsr1Notifier)
		delete m_pUsr1Notifier;
#endif

	// Drop any widgets around (not really necessary)...
	if (m_pMixer)
		delete m_pMixer;
	if (m_pConnections)
		delete m_pConnections;
	if (m_pFiles)
		delete m_pFiles;
	if (m_pMessages)
		delete m_pMessages;
	if (m_pTracks)
		delete m_pTracks;

	// Get select mode action group down.
	if (m_pSelectModeActionGroup)
		delete m_pSelectModeActionGroup;

	// Reclaim status items palettes...
	for (int i = 0; i < PaletteItems; ++i)
		delete m_paletteItems[i];

	// Custom tempo cursor.
	if (m_pTempoCursor)
		delete m_pTempoCursor;

	// Remove midi controllers.
	if (m_pMidiControl)
		delete m_pMidiControl;

	// Remove OSC control server (TESTING).
	if (m_pOscControl)
		delete m_pOscControl;

	// Pseudo-singleton reference shut-down.
	g_pMainForm = NULL;
}


// kind of singleton reference.
qtractorMainForm *qtractorMainForm::getInstance (void)
{
	return g_pMainForm;
}


// Make and set a proper setup options step.
void qtractorMainForm::setup ( qtractorOptions *pOptions )
{
	// We got options?
	m_pOptions = pOptions;

	// Some child/dockable forms are to be created right now.
	m_pFiles = new qtractorFiles(this);
	m_pFiles->audioListView()->setRecentDir(m_pOptions->sAudioDir);
	m_pFiles->midiListView()->setRecentDir(m_pOptions->sMidiDir);
	m_pMessages = new qtractorMessages(this);

	// Setup messages logging appropriately...
	m_pMessages->setLogging(
		m_pOptions->bMessagesLog,
		m_pOptions->sMessagesLogPath);

	// What style do we create tool childs?
	QWidget *pParent = NULL;
	Qt::WindowFlags wflags = Qt::Window
		| Qt::CustomizeWindowHint
		| Qt::WindowTitleHint
		| Qt::WindowSystemMenuHint
		| Qt::WindowMinMaxButtonsHint
		| Qt::WindowCloseButtonHint;
	if (m_pOptions->bKeepToolsOnTop) {
        pParent = this;
		wflags |= Qt::Tool;
	}
	// Other child/tools forms are also created right away...
	m_pConnections = new qtractorConnections(pParent, wflags);
	m_pMixer = new qtractorMixer(pParent, wflags);

	// Make those primordially docked...
	addDockWidget(Qt::RightDockWidgetArea, m_pFiles, Qt::Vertical);
	addDockWidget(Qt::BottomDockWidgetArea, m_pMessages, Qt::Horizontal);

	// Time to create the main session track list view...
	m_pTracks = new qtractorTracks(this);

	// Make it shine :-)
	setCentralWidget(m_pTracks);

	// Set message defaults...
	updateMessagesFont();
	updateMessagesLimit();
	updateMessagesCapture();

	// Track view select mode...
	qtractorTrackView::SelectMode selectMode;
	switch (pOptions->iTrackViewSelectMode) {
	case 2:
		selectMode = qtractorTrackView::SelectRect;
		m_ui.editSelectModeRectAction->setChecked(true);
		break;
	case 1:
		selectMode = qtractorTrackView::SelectRange;
		m_ui.editSelectModeRangeAction->setChecked(true);
		break;
	case 0:
	default:
		selectMode = qtractorTrackView::SelectClip;
		m_ui.editSelectModeClipAction->setChecked(true);
		break;
	}

	qtractorTrackView *pTrackView = m_pTracks->trackView();
	pTrackView->setSelectMode(selectMode);
	pTrackView->setDropSpan(m_pOptions->bTrackViewDropSpan);
	pTrackView->setSnapGrid(m_pOptions->bTrackViewSnapGrid);
	pTrackView->setToolTips(m_pOptions->bTrackViewToolTips);

	// Initial zoom mode...
	m_pTracks->setZoomMode(m_pOptions->iZoomMode);

	// Initial auto time-stretching mode...
	m_pSession->setAutoTimeStretch(m_pOptions->bAudioAutoTimeStretch);

	// Initial decorations toggle state.
	m_ui.viewMenubarAction->setChecked(m_pOptions->bMenubar);
	m_ui.viewStatusbarAction->setChecked(m_pOptions->bStatusbar);
	m_ui.viewToolbarFileAction->setChecked(m_pOptions->bFileToolbar);
	m_ui.viewToolbarEditAction->setChecked(m_pOptions->bEditToolbar);
	m_ui.viewToolbarTrackAction->setChecked(m_pOptions->bTrackToolbar);
	m_ui.viewToolbarViewAction->setChecked(m_pOptions->bViewToolbar);
	m_ui.viewToolbarOptionsAction->setChecked(m_pOptions->bOptionsToolbar);
	m_ui.viewToolbarTransportAction->setChecked(m_pOptions->bTransportToolbar);
	m_ui.viewToolbarTimeAction->setChecked(m_pOptions->bTimeToolbar);
	m_ui.viewToolbarThumbAction->setChecked(m_pOptions->bThumbToolbar);
	m_ui.viewSnapGridAction->setChecked(pOptions->bTrackViewSnapGrid);
	m_ui.viewToolTipsAction->setChecked(pOptions->bTrackViewToolTips);

	m_ui.transportMetroAction->setChecked(m_pOptions->bMetronome);
	m_ui.transportFollowAction->setChecked(m_pOptions->bFollowPlayhead);
	m_ui.transportAutoBackwardAction->setChecked(m_pOptions->bAutoBackward);
	m_ui.transportContinueAction->setChecked(m_pOptions->bContinuePastEnd);

	m_ui.trackAutoMonitorAction->setChecked(m_pOptions->bAutoMonitor);

	// Initial decorations visibility state.
	viewMenubar(m_pOptions->bMenubar);
	viewStatusbar(m_pOptions->bStatusbar);
	viewToolbarFile(m_pOptions->bFileToolbar);
	viewToolbarEdit(m_pOptions->bEditToolbar);
	viewToolbarTrack(m_pOptions->bTrackToolbar);
	viewToolbarView(m_pOptions->bViewToolbar);
	viewToolbarOptions(m_pOptions->bOptionsToolbar);
	viewToolbarTransport(m_pOptions->bTransportToolbar);
	viewToolbarTime(m_pOptions->bTimeToolbar);
	viewToolbarThumb(m_pOptions->bThumbToolbar);

	// Restore whole dock windows state.
	QByteArray aDockables = m_pOptions->settings().value(
		"/Layout/DockWindows").toByteArray();
	if (aDockables.isEmpty()) {
		// Some windows are forced initially as is...
		insertToolBarBreak(m_ui.transportToolbar);
	} else {
		// Make it as the last time.
		restoreState(aDockables);
	}

	// Try to restore old window positioning.
	m_pOptions->loadWidgetGeometry(this, true);
	m_pOptions->loadWidgetGeometry(m_pMixer);
	m_pOptions->loadWidgetGeometry(m_pConnections);

	// Load MIDI controller configuration files...
	QStringListIterator it(m_pOptions->midiControlFiles);
	while (it.hasNext())
		m_pMidiControl->loadDocument(it.next());

	// Load instrument definition files...
	QStringListIterator iter(m_pOptions->instrumentFiles);
	while (iter.hasNext())
		(m_pSession->instruments())->load(iter.next());

	// Load custom meter colors, if any...
	int iColor;
	for (iColor = 0; iColor < m_pOptions->audioMeterColors.count(); ++iColor)
		qtractorAudioMeter::setColor(iColor,
			QColor(m_pOptions->audioMeterColors[iColor]));
	for (iColor = 0; iColor < m_pOptions->midiMeterColors.count(); ++iColor)
		qtractorMidiMeter::setColor(iColor,
			QColor(m_pOptions->midiMeterColors[iColor]));

	// Primary startup stabilization...
	updateRecentFilesMenu();
	updatePeakAutoRemove();
	updateDisplayFormat();
	updatePluginPaths();
	updateTransportMode();
	updateAudioPlayer();
	updateAudioMetronome();
	updateMidiControlModes();
	updateMidiQueueTimer();
	updateMidiControl();
	updateMidiMetronome();

	// FIXME: This is what it should ever be,
	// make it right from this very moment...
	qtractorAudioFileFactory::setDefaultType(
		m_pOptions->sAudioCaptureExt,
		m_pOptions->iAudioCaptureType,
		m_pOptions->iAudioCaptureFormat,
		m_pOptions->iAudioCaptureQuality);
	qtractorMidiClip::setDefaultFormat(
		m_pOptions->iMidiCaptureFormat);
	// Set default MIDI (plugin) instrument audio output mode.
	qtractorMidiManager::setDefaultAudioOutputBus(
		m_pOptions->bAudioOutputBus);
	// Set default audio-buffer quality...
	qtractorAudioBuffer::setResampleType(m_pOptions->iAudioResampleType);
	qtractorAudioBuffer::setWsolaTimeStretch(m_pOptions->bAudioWsolaTimeStretch);
	qtractorAudioBuffer::setWsolaQuickSeek(m_pOptions->bAudioWsolaQuickSeek);

	// Load (action) keyboard shortcuts...
	m_pOptions->loadActionShortcuts(this);

	// Initial thumb-view background (empty)
	m_pThumbView->updateContents();

	// Is any session identification to get loaded?
	bool bSessionId = !m_pOptions->sSessionId.isEmpty();
	if (bSessionId) {
		qtractorAudioEngine *pAudioEngine = m_pSession->audioEngine();
		if (pAudioEngine) {
			pAudioEngine->setSessionId(m_pOptions->sSessionId);
			m_pOptions->sSessionId.clear();
		}
	}

	// Is any session pending to be loaded?
	if (!m_pOptions->sSessionFile.isEmpty()) {
		// Just load the prabable startup session...
		if (loadSessionFileEx(m_pOptions->sSessionFile, false, !bSessionId))
			m_pOptions->sSessionFile.clear();
	} else {
		// Change to last known session dir...
		if (!m_pOptions->sSessionDir.isEmpty()) {
			if (!QDir::setCurrent(m_pOptions->sSessionDir)) {
				appendMessagesError(
					tr("Could not set default session directory:\n\n"
					"%1\n\nSorry.").arg(m_pOptions->sSessionDir));
				m_pOptions->sSessionDir.clear();
			}
		}
		// Open up with a new empty session...
		newSession();
	}

	// Final widget slot connections....
	QObject::connect(m_pFiles->toggleViewAction(),
		SIGNAL(triggered(bool)),
		SLOT(stabilizeForm()));
	QObject::connect(m_pMessages->toggleViewAction(),
		SIGNAL(triggered(bool)),
		SLOT(stabilizeForm()));
	QObject::connect(m_pConnections->connectForm(),
		SIGNAL(connectChanged()),
		SLOT(contentsChanged()));
	QObject::connect(m_pMixer->trackRack(),
		SIGNAL(selectionChanged()),
		SLOT(mixerSelectionChanged()));
	QObject::connect(m_pFiles->audioListView(),
		SIGNAL(selected(const QString&, int, bool)),
		SLOT(selectAudioFile(const QString&, int, bool)));
	QObject::connect(m_pFiles->audioListView(),
		SIGNAL(activated(const QString&, int)),
		SLOT(activateAudioFile(const QString&, int)));
	QObject::connect(m_pFiles->midiListView(),
		SIGNAL(selected(const QString&, int, bool)),
		SLOT(selectMidiFile(const QString&, int, bool)));
	QObject::connect(m_pFiles->midiListView(),
		SIGNAL(activated(const QString&, int)),
		SLOT(activateMidiFile(const QString&, int)));
	QObject::connect(m_pFiles->audioListView(),
		SIGNAL(contentsChanged()),
		SLOT(contentsChanged()));
	QObject::connect(m_pFiles->midiListView(),
		SIGNAL(contentsChanged()),
		SLOT(contentsChanged()));
	QObject::connect(m_pTracks->trackList(),
		SIGNAL(selectionChanged()),
		SLOT(trackSelectionChanged()));

	// Other dedicated signal/slot connections...
	QObject::connect(m_pTracks->trackView(),
		SIGNAL(contentsMoving(int,int)),
		m_pThumbView, SLOT(updateThumb()));

	// Make it ready :-)
	statusBar()->showMessage(tr("Ready"), 3000);
 
	// Register the first timer slot.
	QTimer::singleShot(QTRACTOR_TIMER_DELAY, this, SLOT(timerSlot()));
}


// LADISH Level 1 -- SIGUSR1 signal handler.
void qtractorMainForm::handle_sigusr1 (void)
{
#ifdef HAVE_SIGNAL_H

	char c;

	if (::read(g_fdUsr1[1], &c, sizeof(c)) > 0)
		saveSession(false);

#endif
}


// Window close event handlers.
bool qtractorMainForm::queryClose (void)
{
	bool bQueryClose = closeSession();

	// Try to save current general state...
	if (m_pOptions) {
		// Some windows default fonts is here on demand too.
		if (bQueryClose && m_pMessages)
			m_pOptions->sMessagesFont = m_pMessages->messagesFont().toString();
		// Save recent directories...
		if (bQueryClose && m_pFiles) {
			m_pOptions->sAudioDir = m_pFiles->audioListView()->recentDir();
			m_pOptions->sMidiDir = m_pFiles->midiListView()->recentDir();
		}
		// Try to save current positioning.
		if (bQueryClose) {
			// Save decorations state.
			m_pOptions->bMenubar = m_ui.menuBar->isVisible();
			m_pOptions->bStatusbar = statusBar()->isVisible();
			m_pOptions->bFileToolbar = m_ui.fileToolbar->isVisible();
			m_pOptions->bEditToolbar = m_ui.editToolbar->isVisible();
			m_pOptions->bTrackToolbar = m_ui.trackToolbar->isVisible();
			m_pOptions->bViewToolbar = m_ui.viewToolbar->isVisible();
			m_pOptions->bOptionsToolbar = m_ui.optionsToolbar->isVisible();
			m_pOptions->bTransportToolbar = m_ui.transportToolbar->isVisible();
			m_pOptions->bTimeToolbar = m_ui.timeToolbar->isVisible();
			m_pOptions->bThumbToolbar = m_ui.thumbToolbar->isVisible();
			m_pOptions->bTrackViewSnapGrid = m_ui.viewSnapGridAction->isChecked();
			m_pOptions->bTrackViewToolTips = m_ui.viewToolTipsAction->isChecked();
			m_pOptions->bMetronome = m_ui.transportMetroAction->isChecked();
			m_pOptions->bFollowPlayhead = m_ui.transportFollowAction->isChecked();
			m_pOptions->bAutoBackward = m_ui.transportAutoBackwardAction->isChecked();
			m_pOptions->bContinuePastEnd = m_ui.transportContinueAction->isChecked();
			m_pOptions->bAutoMonitor = m_ui.trackAutoMonitorAction->isChecked();
			// Final zoom mode...
			if (m_pTracks)
				m_pOptions->iZoomMode = m_pTracks->zoomMode();
			// Save instrument definition file list...
			m_pOptions->instrumentFiles = (m_pSession->instruments())->files();
			// Save custom meter colors, if any...
			int iColor;
			m_pOptions->audioMeterColors.clear();
			for (iColor = 0; iColor < qtractorAudioMeter::ColorCount - 2; ++iColor)
				m_pOptions->audioMeterColors.append(
					qtractorAudioMeter::color(iColor).name());
			m_pOptions->midiMeterColors.clear();
			for (iColor = 0; iColor < qtractorMidiMeter::ColorCount - 2; ++iColor)
				m_pOptions->midiMeterColors.append(
					qtractorMidiMeter::color(iColor).name());
			// Make sure there will be defaults...
			m_pOptions->iSnapPerBeat = m_pSnapPerBeatComboBox->currentIndex();
			m_pOptions->fTempo = m_pTempoSpinBox->tempo();
			m_pOptions->iBeatsPerBar = m_pTempoSpinBox->beatsPerBar();
			m_pOptions->iBeatDivisor = m_pTempoSpinBox->beatDivisor();
			// Save the dock windows state.
			m_pOptions->settings().setValue("/Layout/DockWindows", saveState());
			// And the main windows state.
			m_pOptions->saveWidgetGeometry(m_pConnections);
			m_pOptions->saveWidgetGeometry(m_pMixer);
			m_pOptions->saveWidgetGeometry(this, true);
		}
	}

	return bQueryClose;
}


void qtractorMainForm::closeEvent ( QCloseEvent *pCloseEvent )
{
	// Let's be sure about that...
	if (queryClose()) {
		pCloseEvent->accept();
		QApplication::quit();
	} else {
		pCloseEvent->ignore();
	}
}


// Window drag-n-drop event handlers.
void qtractorMainForm::dragEnterEvent ( QDragEnterEvent* pDragEnterEvent )
{
	// Accept external drags only...
	if (pDragEnterEvent->source() == NULL
		&& pDragEnterEvent->mimeData()->hasUrls()) {
		pDragEnterEvent->accept();
	} else {
		pDragEnterEvent->ignore();
	}
}


void qtractorMainForm::dropEvent ( QDropEvent* pDropEvent )
{
	// Accept externally originated drops only...
	if (pDropEvent->source())
		return;

	const QMimeData *pMimeData = pDropEvent->mimeData();
	if (pMimeData->hasUrls()) {
		QString sFilename = pMimeData->urls().first().toLocalFile();
		// Close current session and try to load the new one...
		if (!sFilename.isEmpty() && closeSession())
			loadSessionFile(sFilename);
	}
}


// Context menu event handler.
void qtractorMainForm::contextMenuEvent( QContextMenuEvent *pEvent )
{
	stabilizeForm();

	// Primordial edit menu should be available...
	m_ui.editMenu->exec(pEvent->globalPos());
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Brainless public property accessors.

// The global session tracks reference.
qtractorTracks *qtractorMainForm::tracks (void) const
{
	return m_pTracks;
}

// The global session file(lists) reference.
qtractorFiles *qtractorMainForm::files (void) const
{
	return m_pFiles;
}

// The global session connections reference.
qtractorConnections *qtractorMainForm::connections (void) const
{
	return m_pConnections;
}

// The global session mixer reference.
qtractorMixer *qtractorMainForm::mixer (void) const
{
	return m_pMixer;
}

// The session thumb-view widget accessor.
qtractorThumbView *qtractorMainForm::thumbView (void) const
{
	return m_pThumbView;
}

// The session transport fast-rolling direction accessor.
int qtractorMainForm::rolling (void) const
{
	return m_iTransportRolling;
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Session file stuff.

// Format the displayable session filename.
QString qtractorMainForm::sessionName ( const QString& sFilename )
{
	bool bCompletePath = (m_pOptions && m_pOptions->bCompletePath);
	QString sSessionName = sFilename;
	if (sSessionName.isEmpty() && m_pSession)
		sSessionName = m_pSession->sessionName();
	if (sSessionName.isEmpty())
		sSessionName = tr("Untitled%1").arg(m_iUntitled);
	else if (!bCompletePath)
		sSessionName = QFileInfo(sSessionName).baseName();
	return sSessionName;
}


// Create a new session file from scratch.
bool qtractorMainForm::newSession (void)
{
	// Check if we can do it.
	if (!closeSession())
		return false;

#ifdef CONFIG_LV2
	qtractorLv2PluginType::slv2_open();
#endif

	// Check whether we start new session
	// based on existing template...
	if (m_pOptions && m_pOptions->bSessionTemplate)
		return loadSessionFileEx(m_pOptions->sSessionTemplatePath, true);

	// Ok, increment untitled count.
	++m_iUntitled;

	// Stabilize form.
	m_sFilename.clear();
//	m_iDirtyCount = 0;
	appendMessages(tr("New session: \"%1\".").arg(sessionName(m_sFilename)));

	// Give us what we got, right now...
	updateSession();

	return true;
}


// Open an existing sampler session.
bool qtractorMainForm::openSession (void)
{
	if (m_pOptions == NULL)
		return false;

	// Ask for the filename to open...
	QString sFilename;

	QString sExt("qtr");
	QStringList filters;
#ifdef CONFIG_LIBZ
	filters.append(tr("Session files (*.%1 *.%2 *.%3)")
		.arg(sExt).arg(qtractorDocument::defaultExt())
		.arg(qtractorDocument::archiveExt()));
#else
	filters.append(tr("Session files (*.%1 *.%2)")
		.arg(sExt).arg(qtractorDocument::defaultExt()));
#endif
	filters.append(tr("Template files (*.%1)")
		.arg(qtractorDocument::templateExt()));
#ifdef CONFIG_LIBZ
	filters.append(tr("Archive files (*.%1)")
		.arg(qtractorDocument::archiveExt()));
#endif
	const QString& sTitle  = tr("Open Session") + " - " QTRACTOR_TITLE;
	const QString& sFilter = filters.join(";;");
#if QT_VERSION < 0x040400
	sFilename = QFileDialog::getOpenFileName(this,
		sTitle, m_pOptions->sSessionDir, sFilter);
#else
	// Construct open-file session/template dialog...
	QFileDialog fileDialog(this,
		sTitle, m_pOptions->sSessionDir, sFilter);
	// Set proper open-file modes...
	fileDialog.setAcceptMode(QFileDialog::AcceptOpen);
	fileDialog.setFileMode(QFileDialog::ExistingFile);
	fileDialog.setHistory(m_pOptions->recentFiles);
	fileDialog.setDefaultSuffix(sExt);
	// Stuff sidebar...
	QList<QUrl> urls(fileDialog.sidebarUrls());
	urls.append(QUrl::fromLocalFile(m_pOptions->sSessionDir));
	fileDialog.setSidebarUrls(urls);
	// Show dialog...
	if (!fileDialog.exec())
		return false;
	// Have the open-file name...
	sFilename = fileDialog.selectedFiles().first();
#endif

	// Have we cancelled?
	if (sFilename.isEmpty())
		return false;

	// Check if we're going to discard safely the current one...
	if (!closeSession())
		return false;

	// Load it right away.
	return loadSessionFile(sFilename);
}


// Save current sampler session with another name.
bool qtractorMainForm::saveSession ( bool bPrompt )
{
	if (m_pOptions == NULL)
		return false;

	// It must be a session name...
	if (m_pSession->sessionName().isEmpty() && !editSession())
		return false;

	// Suggest a filename, if there's none...
	QString sFilename = m_sFilename;

	if (sFilename.isEmpty()) {
		sFilename = QFileInfo(m_pOptions->sSessionDir,
			qtractorSession::sanitize(m_pSession->sessionName())).filePath();
		bPrompt = true;
	}

	// Ask for the file to save...
	if (bPrompt) {
		// Prompt the guy...
		QString sExt("qtr");
		QStringList filters;
	#ifdef CONFIG_LIBZ
		filters.append(tr("Session files (*.%1 *.%2 *.%3)")
			.arg(sExt).arg(qtractorDocument::defaultExt())
			.arg(qtractorDocument::archiveExt()));
	#else
		filters.append(tr("Session files (*.%1 *.%2)")
			.arg(sExt).arg(qtractorDocument::defaultExt()));
	#endif
		filters.append(tr("Template files (*.%1)")
			.arg(qtractorDocument::templateExt()));
	#ifdef CONFIG_LIBZ
		filters.append(tr("Archive files (*.%1)")
			.arg(qtractorDocument::archiveExt()));
	#endif
		const QString& sTitle  = tr("Save Session") + " - " QTRACTOR_TITLE;
		const QString& sFilter = filters.join(";;");
	#if QT_VERSION < 0x040400
		sFilename = QFileDialog::getSaveFileName(this,
			sTitle, sFilename, sFilter);
	#else
		// Construct save-file session/template dialog...
		QFileDialog fileDialog(this,
			sTitle, sFilename, sFilter);
		// Set proper save-file modes...
		fileDialog.setAcceptMode(QFileDialog::AcceptSave);
		fileDialog.setFileMode(QFileDialog::AnyFile);
		fileDialog.setHistory(m_pOptions->recentFiles);
		fileDialog.setDefaultSuffix(sExt);
		// Stuff sidebar...
		QList<QUrl> urls(fileDialog.sidebarUrls());
		urls.append(QUrl::fromLocalFile(m_pOptions->sSessionDir));
		fileDialog.setSidebarUrls(urls);
		// Show save-file dialog...
		if (!fileDialog.exec())
			return false;
		// Have the save-file name...
		sFilename = fileDialog.selectedFiles().first();
		// Check whether we're on the template or archive filter...
		switch (filters.indexOf(fileDialog.selectedNameFilter())) {
		case 1:
			sExt = qtractorDocument::templateExt();
			break;
		case 2:
			sExt = qtractorDocument::archiveExt();
			break;
		}
	#endif
		// Have we cancelled it?
		if (sFilename.isEmpty())
			return false;
		// Enforce extension...
		if (QFileInfo(sFilename).suffix().isEmpty()) {
			sFilename += '.' + sExt;
			// Check if already exists...
			if (sFilename != m_sFilename && QFileInfo(sFilename).exists()) {
				if (QMessageBox::warning(this,
					tr("Warning") + " - " QTRACTOR_TITLE,
					tr("The file already exists:\n\n"
					"\"%1\"\n\n"
					"Do you want to replace it?")
					.arg(sFilename),
					QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Cancel)
					return false;
			}
		}
	}

	// Save it right away.
	return saveSessionFile(sFilename);
}


// Edit session properties.
bool qtractorMainForm::editSession (void)
{
	// Session Properties...
	qtractorSessionForm sessionForm(this);
	sessionForm.setSession(m_pSession);
	if (!sessionForm.exec())
		return false;

	// If currently playing, we need to do a stop and go...
	bool bPlaying = m_pSession->isPlaying();
	if (bPlaying)
		m_pSession->lock();

	// Take care of session name changes...
	const QString sOldSessionName = m_pSession->sessionName();

	// Now, express the change as a undoable command...
	m_pSession->execute(
		new qtractorSessionEditCommand(m_pSession, sessionForm.properties()));

	// If session name has changed, we'll prompt
	// for correct filename when save is triggered...
	if (m_pSession->sessionName() != sOldSessionName)
		m_sFilename.clear();

	// Restore playback state, if needed...
	if (bPlaying)
		m_pSession->unlock();

	// Transport status needs an update too...
	++m_iTransportUpdate;

	// Done.
	return true;
}



// Close current session.
bool qtractorMainForm::closeSession (void)
{
	bool bClose = true;

	// Are we dirty enough to prompt it?
	if (bClose && m_iDirtyCount > 0) {
		switch (QMessageBox::warning(this,
			tr("Warning") + " - " QTRACTOR_TITLE,
			tr("The current session has been changed:\n\n"
			"\"%1\"\n\n"
			"Do you want to save the changes?")
			.arg(sessionName(m_sFilename)),
			QMessageBox::Save |
			QMessageBox::Discard |
			QMessageBox::Cancel)) {
		case QMessageBox::Save:
			bClose = saveSession(false);
			// Fall thru....
		case QMessageBox::Discard:
			break;
		default:    // Cancel.
			bClose = false;
			break;
		}
	}

	// If we may close it, do it!
	if (bClose) {
		// Just in case we were in the middle of something...
		setPlaying(false);
		// Reset all dependables to default.
		m_pMixer->clear();
		m_pFiles->clear();
		// Close session engines.
		m_pSession->close();
		m_pSession->clear();
		m_pTempoCursor->clear();
		// And last but not least.
		m_pConnections->clear();
		m_pTracks->clear();
		// Reset playhead.
		m_iPlayHead = 0;
	#ifdef CONFIG_LV2
		qtractorLv2PluginType::slv2_close();
	#endif
	#ifdef CONFIG_LIBZ
		// Is it time to cleanup extracted archives?
		const QStringList& paths = qtractorDocument::extractedArchives();
		if (!paths.isEmpty()) {
			bool bRemove = true;
			if (m_pOptions && m_pOptions->bConfirmRemove) {
				if (QMessageBox::warning(this,
					tr("Warning") + " - " QTRACTOR_TITLE,
					tr("About to remove archive directory:\n\n"
					"\"%1\"\n\n"
					"Are you sure?")
					.arg(paths.join("\",\n\"")),
					QMessageBox::Yes | QMessageBox::No) == QMessageBox::No)
					bRemove = false;
			}
			qtractorDocument::clearExtractedArchives(bRemove);
		}
	#endif
		// Some defaults are due...
		if (m_pOptions) {
			m_pSession->setSessionDir(m_pOptions->sSessionDir);
			m_pSession->setSnapPerBeat(
				qtractorTimeScale::snapFromIndex(m_pOptions->iSnapPerBeat));
			m_pSession->setTempo(m_pOptions->fTempo);
			m_pSession->setBeatsPerBar(m_pOptions->iBeatsPerBar);
			m_pSession->setBeatDivisor(m_pOptions->iBeatDivisor);
		}
		// We're now clean, for sure.
		m_iDirtyCount = 0;
		appendMessages(tr("Session closed."));
	}

	return bClose;
}


// Load a session from specific file path.
bool qtractorMainForm::loadSessionFileEx (
	const QString& sFilename, bool bTemplate, bool bUpdate )
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::loadSessionFileEx(\"%s\", %d, %d)",
		sFilename.toUtf8().constData(), int(bTemplate), int(bUpdate));
#endif

	// Flag whether we're about to save as template or archive...
	QFileInfo info(sFilename);
	int iFlags = qtractorDocument::Default;
	const QString& sSuffix = info.suffix();
	if (sSuffix == qtractorDocument::templateExt() || bTemplate) {
		iFlags |= qtractorDocument::Template;
		bTemplate = true;
	}
#ifdef CONFIG_LIBZ
	if (sSuffix == qtractorDocument::archiveExt()) {
		iFlags |= qtractorDocument::Archive;
		// Take special precaution for already
		// existing archive directory...
		QString sPath;
		if (bUpdate) {
			sPath = info.path();
		} else {
			sPath = QDir::temp().path();
			iFlags |= qtractorDocument::Temporary;
		}
		info.setFile(sPath + QDir::separator() + info.completeBaseName());
		if (info.exists() && info.isDir()) {
			if (QMessageBox::warning(this,
				tr("Warning") + " - " QTRACTOR_TITLE,
				tr("The directory already exists:\n\n"
				"\"%1\"\n\n"
				"Do you want to replace it?")
				.arg(info.filePath()),
				QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Cancel) {
				++m_iUntitled;
				m_sFilename.clear();
				updateSession();
				return false;
			}
		}
	}
#endif

	// Tell the world we'll take some time...
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	appendMessages(tr("Opening \"%1\"...").arg(sFilename));

#ifdef CONFIG_LV2
	qtractorLv2PluginType::slv2_open();
#endif
		
	// Read the file.
	QDomDocument doc("qtractorSession");
	bool bResult = qtractorSessionDocument(&doc, m_pSession, m_pFiles)
		.load(sFilename, qtractorDocument::Flags(iFlags));

	// We're formerly done.
	QApplication::restoreOverrideCursor();

	if (bResult) {
		// Got something loaded...
		// we're not dirty anymore.
		if (!bTemplate) {
			if (bUpdate)
				updateRecentFiles(sFilename);
		//	m_iDirtyCount = 0;
		}
		// Save as default session directory...
		if (m_pOptions && bUpdate) {
			m_pOptions->sSessionDir = QFileInfo(sFilename).absolutePath();
			m_pOptions->saveOptions();
		}	
	} else {
		// Something went wrong...
		appendMessagesError(
			tr("Session could not be loaded\n"
			"from \"%1\".\n\n"
			"Sorry.").arg(sFilename));
	}

	// Stabilize form title...
	if (bTemplate) {
		++m_iUntitled;
		m_sFilename.clear();
	} else {
		m_sFilename = sFilename;
	}

	appendMessages(tr("Open session: \"%1\".").arg(sessionName(sFilename)));

	// Now we'll try to create (update) the whole GUI session.
	updateSession();

	return bResult;
}


bool qtractorMainForm::loadSessionFile ( const QString& sFilename )
{
	return loadSessionFileEx(sFilename, false, true);
}


// Save current session to specific file path.
bool qtractorMainForm::saveSessionFileEx (
	const QString& sFilename, bool bTemplate, bool bUpdate )
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::saveSessionFileEx(\"%s\", %d, %d)",
		sFilename.toUtf8().constData(), int(bTemplate), int(bUpdate));
#endif

	// Flag whether we're about to save as template or archive...
	int iFlags = qtractorDocument::Default;
	const QString& sSuffix = QFileInfo(sFilename).suffix();
	if (sSuffix == qtractorDocument::templateExt() || bTemplate) {
		iFlags |= qtractorDocument::Template;
		bTemplate = true;
	}
#ifdef CONFIG_LIBZ
	if (sSuffix == qtractorDocument::archiveExt())
		iFlags |= qtractorDocument::Archive;
#endif

	// Tell the world we'll take some time...
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	appendMessages(tr("Saving \"%1\"...").arg(sFilename));
	
	// Trap dirty clips (only MIDI at this time...)
	for (qtractorTrack *pTrack = m_pSession->tracks().first();
			pTrack; pTrack = pTrack->next()) {
		// Only MIDI track/clips...
		if (pTrack->trackType() != qtractorTrack::Midi)
			continue;
		for (qtractorClip *pClip = pTrack->clips().first();
				pClip; pClip = pClip->next()) {
			// Are any dirty changes pending commit?
			if (pClip->isDirty()) {
				qtractorMidiClip *pMidiClip
					= static_cast<qtractorMidiClip *> (pClip);
				if (pMidiClip) {
					// Have a new filename revision...
					const QString& sFilename
						= pMidiClip->createFilePathRevision();
					// Save/replace the clip track...
					qtractorMidiFile::saveCopyFile(sFilename,
						pMidiClip->filename(),
						pMidiClip->trackChannel(),
						pMidiClip->format(),
						pMidiClip->sequence(),
						m_pSession->timeScale(),
						m_pSession->tickFromFrame(pMidiClip->clipStart()));
					// Pre-commit dirty changes...
					pMidiClip->setFilename(sFilename);
					pMidiClip->setDirty(false);
					// And refresh any eventual editor out there...
					pMidiClip->updateEditor(true);
					// Reference for immediate file addition...
					addMidiFile(sFilename);
				}
			}
		}
	}

	// Write the file...
	QDomDocument doc("qtractorSession");
	bool bResult = qtractorSessionDocument(&doc, m_pSession, m_pFiles)
		.save(sFilename, qtractorDocument::Flags(iFlags));

#ifdef CONFIG_LIBZ
	if ((iFlags & qtractorDocument::Archive) == 0)
		qtractorDocument::clearExtractedArchives();
#endif

	// We're formerly done.
	QApplication::restoreOverrideCursor();

	if (bResult) {
		// Got something saved...
		// we're not dirty anymore.
		if (!bTemplate) {
			if (bUpdate)
				updateRecentFiles(sFilename);
			m_iDirtyCount = 0;
		}
		// Save some default session properties...
		if (m_pOptions && bUpdate) {
			qtractorAudioEngine *pAudioEngine = m_pSession->audioEngine();
			if (pAudioEngine)
				m_pOptions->iTransportMode = int(pAudioEngine->transportMode());
			qtractorMidiEngine *pMidiEngine = m_pSession->midiEngine();
			if (pMidiEngine) {
				m_pOptions->iMidiMmcMode   = int(pMidiEngine->mmcMode());
				m_pOptions->iMidiMmcDevice = int(pMidiEngine->mmcDevice());
				m_pOptions->iMidiSppMode   = int(pMidiEngine->sppMode());
				m_pOptions->iMidiClockMode = int(pMidiEngine->clockMode());
			}
			m_pOptions->sSessionDir = QFileInfo(sFilename).absolutePath();
			m_pOptions->saveOptions();
		}
	} else {
		// Something went wrong...
		appendMessagesError(
			tr("Session could not be saved\n"
			"to \"%1\".\n\n"
			"Sorry.").arg(sFilename));
	}

	// Stabilize form title...
	if (!bTemplate)
		m_sFilename = sFilename;

	appendMessages(tr("Save session: \"%1\".").arg(sessionName(sFilename)));

	// Show static results...
	stabilizeForm();

	return bResult;
}


bool qtractorMainForm::saveSessionFile ( const QString& sFilename )
{
	return saveSessionFileEx(sFilename, false, true);
}


//-------------------------------------------------------------------------
// qtractorMainForm -- File Action slots.

// Create a new sampler session.
void qtractorMainForm::fileNew (void)
{
	// Of course we'll start clean new.
	newSession();
}


// Open an existing sampler session.
void qtractorMainForm::fileOpen (void)
{
	// Open it right away.
	openSession();
}


// Open a recent file session.
void qtractorMainForm::fileOpenRecent (void)
{
	// Retrive filename index from action data...
	QAction *pAction = qobject_cast<QAction *> (sender());
	if (pAction && m_pOptions) {
		int iIndex = pAction->data().toInt();
		if (iIndex >= 0 && iIndex < m_pOptions->recentFiles.count()) {
			QString sFilename = m_pOptions->recentFiles[iIndex];
			// Check if we can safely close the current session...
			if (!sFilename.isEmpty() && closeSession())
				loadSessionFile(sFilename);
		}
	}
}


// Save current sampler session.
void qtractorMainForm::fileSave (void)
{
	// Save it right away.
	saveSession(false);
}


// Save current sampler session with another name.
void qtractorMainForm::fileSaveAs (void)
{
	// Save it right away, maybe with another name.
	saveSession(true);
}


// Edit session properties.
void qtractorMainForm::fileProperties (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::fileProperties()");
#endif

	// Session Properties...
	editSession();
}


// Exit application program.
void qtractorMainForm::fileExit (void)
{
	// Go for close the whole thing.
	close();
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Edit Action slots.

// Undo last action.
void qtractorMainForm::editUndo (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editUndo()");
#endif

	(m_pSession->commands())->undo();
}


// Redo last action.
void qtractorMainForm::editRedo (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editRedo()");
#endif

	(m_pSession->commands())->redo();
}


// Cut selection to clipboard.
void qtractorMainForm::editCut (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editCut()");
#endif

	// Cut from files...
	if (m_pFiles && m_pFiles->hasFocus())
		m_pFiles->cutItemSlot();
	else
	// Cut selection...
	if (m_pTracks)
		m_pTracks->cutClipboard();
}


// Copy selection to clipboard.
void qtractorMainForm::editCopy (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editCopy()");
#endif

	// Copy from files...
	if (m_pFiles && m_pFiles->hasFocus())
		m_pFiles->copyItemSlot();
	else
	// Copy selection...
	if (m_pTracks)
		m_pTracks->copyClipboard();

	stabilizeForm();
}


// Paste clipboard contents.
void qtractorMainForm::editPaste (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editPaste()");
#endif

	// Paste to files...
	if (m_pFiles && m_pFiles->hasFocus())
		m_pFiles->pasteItemSlot();
	else
	// Paste selection...
	if (m_pTracks)
		m_pTracks->pasteClipboard();
}


// Paste/repeat clipboard contents.
void qtractorMainForm::editPasteRepeat (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editPasteRepeat()");
#endif

	// Paste/repeat selection...
	if (m_pTracks)
		m_pTracks->pasteRepeatClipboard();
}


// Delete selection.
void qtractorMainForm::editDelete (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editDelete()");
#endif

	// Delete from files...
	if (m_pFiles && m_pFiles->hasFocus())
		m_pFiles->deleteItemSlot();
	else
	// Delete selection...
	if (m_pTracks)
		m_pTracks->deleteClipSelect();
}


// Set selection to whole clip mode.
void qtractorMainForm::editSelectModeClip (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editSelectModeClip()");
#endif

	// Select clip mode...
	if (m_pTracks)
		m_pTracks->trackView()->setSelectMode(qtractorTrackView::SelectClip);
	if (m_pOptions)
		m_pOptions->iTrackViewSelectMode = 0;

	stabilizeForm();
}


// Set selection to range mode.
void qtractorMainForm::editSelectModeRange (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editSelectModeRange()");
#endif

	// Select clip mode...
	if (m_pTracks)
		m_pTracks->trackView()->setSelectMode(qtractorTrackView::SelectRange);
	if (m_pOptions)
		m_pOptions->iTrackViewSelectMode = 1;

	stabilizeForm();
}


// Set selection to rectangularmode.
void qtractorMainForm::editSelectModeRect (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editSelectModeRect()");
#endif

	// Select clip mode...
	if (m_pTracks)
		m_pTracks->trackView()->setSelectMode(qtractorTrackView::SelectRect);
	if (m_pOptions)
		m_pOptions->iTrackViewSelectMode = 2;

	stabilizeForm();
}


// Mark all as selected.
void qtractorMainForm::editSelectAll (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editSelectAll()");
#endif

	// Select all...
	if (m_pTracks)
		m_pTracks->selectAll();

	stabilizeForm();
}


// Mark all as unselected.
void qtractorMainForm::editSelectNone (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editSelectNone()");
#endif

	// Select nothing...
	if (m_pTracks)
		m_pTracks->selectAll(false);

	stabilizeForm();
}


// Invert current selection.
void qtractorMainForm::editSelectInvert (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editSelectInvert()");
#endif

	// Invert selection...
	if (m_pTracks)
		m_pTracks->selectInvert();

	stabilizeForm();
}


// Mark track as selected.
void qtractorMainForm::editSelectTrack (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editSelectTrack()");
#endif

	// Select current track...
	if (m_pTracks)
		m_pTracks->selectCurrentTrack();

	stabilizeForm();
}


// Mark track-range as selected.
void qtractorMainForm::editSelectTrackRange (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editSelectTrackRange()");
#endif

	// Select track-range...
	if (m_pTracks)
		m_pTracks->selectCurrentTrackRange();

	stabilizeForm();
}


// Mark range as selected.
void qtractorMainForm::editSelectRange (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editSelectRange()");
#endif

	// Select edit-range...
	if (m_pTracks)
		m_pTracks->selectEditRange();

	stabilizeForm();
}


// Enter in clip create mode.
void qtractorMainForm::editClipNew (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipNew()");
#endif

	// New clip: we must have a session name...
	if (m_pSession->sessionName().isEmpty() && !editSession())
		return;

	// Start editing a new clip...
	if (m_pTracks)
		m_pTracks->newClip();
}


// Enter in clip edit mode.
void qtractorMainForm::editClipEdit (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipEdit()");
#endif

	// Start editing the current clip, if any...
	if (m_pTracks)
		m_pTracks->editClip();
}


// Split current clip at playhead.
void qtractorMainForm::editClipSplit (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipSplit()");
#endif

	// Split current clip, if any...
	if (m_pTracks)
		m_pTracks->splitClip();
}


// Merge selected (MIDI) clips.
void qtractorMainForm::editClipMerge (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipMerge()");
#endif

	// Merge clip selection, if any...
	if (m_pTracks)
		m_pTracks->mergeClips();
}


// Normalize current clip.
void qtractorMainForm::editClipNormalize (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipNormalize()");
#endif

	// Normalize current clip, if any...
	if (m_pTracks)
		m_pTracks->normalizeClip();
}


// Quantize current clip.
void qtractorMainForm::editClipToolsQuantize (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipToolsQuantize()");
#endif

	// Quantize current clip events, if any...
	if (m_pTracks)
		m_pTracks->executeClipTool(qtractorMidiEditor::Quantize);
}


// Transpose current clip.
void qtractorMainForm::editClipToolsTranspose (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipToolsTranspose()");
#endif

	// Tranpose current clip events, if any...
	if (m_pTracks)
		m_pTracks->executeClipTool(qtractorMidiEditor::Transpose);
}


// Normalize current clip.
void qtractorMainForm::editClipToolsNormalize (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipToolsNormalize()");
#endif

	// Normalize current clip events, if any...
	if (m_pTracks)
		m_pTracks->executeClipTool(qtractorMidiEditor::Normalize);
}


// Randomize current clip.
void qtractorMainForm::editClipToolsRandomize (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipToolsRandomize()");
#endif

	// Randomize current clip events, if any...
	if (m_pTracks)
		m_pTracks->executeClipTool(qtractorMidiEditor::Randomize);
}


// Resize current clip.
void qtractorMainForm::editClipToolsResize (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipToolsResize()");
#endif

	// Resize current clip events, if any...
	if (m_pTracks)
		m_pTracks->executeClipTool(qtractorMidiEditor::Resize);
}


// Rescale current clip.
void qtractorMainForm::editClipToolsRescale (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipToolsRescale()");
#endif

	// Rescale  current clip events, if any...
	if (m_pTracks)
		m_pTracks->executeClipTool(qtractorMidiEditor::Rescale);
}


// Timeshift current clip.
void qtractorMainForm::editClipToolsTimeshift (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipToolsTimeshift()");
#endif

	// Timeshift current clip events, if any...
	if (m_pTracks)
		m_pTracks->executeClipTool(qtractorMidiEditor::Timeshift);
}


// Adjust current tempo from clip selection or interactive tapping...
void qtractorMainForm::editClipTempo (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipTempo()");
#endif

	qtractorTempoAdjustForm form(this);
	unsigned long iRangeStart  = m_pSession->editHead();
	unsigned long iRangeLength = m_pSession->editTail() - iRangeStart;
	if (m_pTracks) {
		qtractorClip *pClip = m_pTracks->currentClip();
		if (pClip) {
			if (m_pTracks->isClipSelected()) {
				iRangeStart  = pClip->clipSelectStart();
				iRangeLength = pClip->clipSelectEnd() - iRangeStart;
			} else {
				iRangeStart  = pClip->clipStart();
				iRangeLength = pClip->clipLength();
			}
		}
	}
	form.setRangeStart(iRangeStart);
	form.setRangeLength(iRangeLength);
	if (form.exec()) {
		transportTempoChanged (
			form.tempo(),
			form.beatsPerBar(),
			form.beatDivisor());
		m_pSession->setEditTail(
			m_pSession->editHead() + form.rangeLength());
		selectionNotifySlot(NULL);
	}
}


// Import (audio) clip.
void qtractorMainForm::editClipImport (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipImport()");
#endif

	// Import (audio) clip(s)...
	if (m_pTracks) {
		// Depending on current track type (default to audio)...
		unsigned long iClipStart = m_pSession->editHead();
		QStringList files;
		qtractorTrack *pTrack = m_pTracks->currentTrack();
		if (pTrack == NULL)
			pTrack = m_pSession->tracks().first();
		if (pTrack && pTrack->trackType() == qtractorTrack::Midi)
			files = m_pFiles->midiListView()->openFileNames();
		else
			files = m_pFiles->audioListView()->openFileNames();
		m_pTracks->importClips(files, iClipStart);
		m_pTracks->trackView()->ensureVisibleFrame(iClipStart);
	}
}


// Export current clip.
void qtractorMainForm::editClipExport (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::editClipExport()");
#endif

	// Export current clip, if any...
	if (m_pTracks)
		m_pTracks->exportClips();
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Track Action slots.
#include "qtractorTrackForm.h"

// Add a new track to session.
void qtractorMainForm::trackAdd (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackAdd()");
#endif

	// Add Track...
	if (m_pTracks)
		m_pTracks->addTrack();
}


// Remove current track from session.
void qtractorMainForm::trackRemove (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackRemove()");
#endif

	// Remove Track...
	if (m_pTracks)
		m_pTracks->removeTrack();
}


// Edit track properties on session.
void qtractorMainForm::trackProperties (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackProperties()");
#endif

	// Track Properties...
	if (m_pTracks)
		m_pTracks->editTrack();
}


// Show current track input bus connections.
void qtractorMainForm::trackInputs (void)
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;
	if (pTrack->inputBus() == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackInputs()");
#endif

	if (m_pConnections)
		m_pConnections->showBus(pTrack->inputBus(), qtractorBus::Input);
}


// Show current track output bus connections.
void qtractorMainForm::trackOutputs (void)
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;
	if (pTrack->outputBus() == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackOutputs()");
#endif

	if (m_pConnections)
		m_pConnections->showBus(pTrack->outputBus(), qtractorBus::Output);
}


// Arm current track for recording.
void qtractorMainForm::trackStateRecord ( bool bOn )
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackStateRecord(%d)", int(bOn));
#endif

	m_pSession->execute(
		new qtractorTrackStateCommand(pTrack, qtractorTrack::Record, bOn));
}


// Mute current track.
void qtractorMainForm::trackStateMute ( bool bOn )
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackStateMute(%d)", int(bOn));
#endif

	m_pSession->execute(
		new qtractorTrackStateCommand(pTrack, qtractorTrack::Mute, bOn));
}


// Solo current track.
void qtractorMainForm::trackStateSolo (  bool bOn  )
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackStateSolo(%d)", int(bOn));
#endif

	m_pSession->execute(
		new qtractorTrackStateCommand(pTrack, qtractorTrack::Solo, bOn));
}


// Monitor current track.
void qtractorMainForm::trackStateMonitor ( bool bOn )
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackStateMonitor(%d)", int(bOn));
#endif

	m_pSession->execute(
		new qtractorTrackMonitorCommand(pTrack, bOn));
}


// Make current the first track on list.
void qtractorMainForm::trackNavigateFirst (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackNavigateFirst()");
#endif

	if (m_pTracks && m_pTracks->trackList())
		(m_pTracks->trackList())->setCurrentTrackRow(0);
}


// Make current the previous track on list.
void qtractorMainForm::trackNavigatePrev (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackNavigatePrev()");
#endif

	if (m_pTracks && m_pTracks->trackList()) {
		int iTrack = (m_pTracks->trackList())->currentTrackRow();
		if (iTrack < 0 && (m_pTracks->trackList())->trackRowCount() > 0)
			iTrack = 1;
		if (iTrack > 0)
			(m_pTracks->trackList())->setCurrentTrackRow(iTrack - 1);
	}
}


// Make current the next track on list.
void qtractorMainForm::trackNavigateNext (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackNavigateNext()");
#endif

	if (m_pTracks && m_pTracks->trackList()) {
		int iTrack = (m_pTracks->trackList())->currentTrackRow();
		if (iTrack < (m_pTracks->trackList())->trackRowCount() - 1)
			(m_pTracks->trackList())->setCurrentTrackRow(iTrack + 1);
	}
}


// Make current the last track on list.
void qtractorMainForm::trackNavigateLast (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackNavigateLast()");
#endif

	if (m_pTracks && m_pTracks->trackList()) {
		int iLastTrack = (m_pTracks->trackList())->trackRowCount() - 1;
		if (iLastTrack >= 0)
			(m_pTracks->trackList())->setCurrentTrackRow(iLastTrack);
	}
}


// Make none current track on list.
void qtractorMainForm::trackNavigateNone (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackNavigateNone()");
#endif

	if (m_pTracks && m_pTracks->trackList())
		(m_pTracks->trackList())->setCurrentTrackRow(-1);
}


// Move current track to top of list.
void qtractorMainForm::trackMoveTop (void)
{
	if (m_pSession == NULL)
		return;

	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackMoveTop()");
#endif

	m_pSession->execute(
		new qtractorMoveTrackCommand(pTrack, m_pSession->tracks().first()));
}


// Move current track up towards the top of list.
void qtractorMainForm::trackMoveUp (void)
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

	qtractorTrack *pNextTrack = pTrack->prev();
	if (pNextTrack == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackMoveUp()");
#endif

	m_pSession->execute(
		new qtractorMoveTrackCommand(pTrack, pNextTrack));
}


// Move current track down towards the bottom of list
void qtractorMainForm::trackMoveDown (void)
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

	qtractorTrack *pNextTrack = pTrack->next();
	if (pNextTrack == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackMoveDown()");
#endif

	m_pSession->execute(
		new qtractorMoveTrackCommand(pTrack, pNextTrack->next()));
}


// Move current track to bottom of list.
void qtractorMainForm::trackMoveBottom (void)
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackMoveBottom()");
#endif

	m_pSession->execute(
		new qtractorMoveTrackCommand(pTrack, NULL));
}


// Auto-monitor current track.
void qtractorMainForm::trackAutoMonitor ( bool bOn )
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackAutoMonitor(%d)", int(bOn));
#endif

	qtractorTrack *pTrack = NULL;
	if (bOn && m_pTracks)
		pTrack = m_pTracks->currentTrack();
	m_pSession->setCurrentTrack(pTrack);
}


// Import some tracks from Audio file.
void qtractorMainForm::trackImportAudio (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackImportAudio()");
#endif

	// Import Audio files into tracks...
	if (m_pTracks) {
		unsigned long iClipStart = m_pSession->editHead();
		m_pTracks->addAudioTracks(
			m_pFiles->audioListView()->openFileNames(), iClipStart);
		m_pTracks->trackView()->ensureVisibleFrame(iClipStart);
	}
}


// Import some tracks from MIDI file.
void qtractorMainForm::trackImportMidi (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackImportMidi()");
#endif

	// Import MIDI files into tracks...
	if (m_pTracks) {
		unsigned long iClipStart = m_pSession->editHead();
		m_pTracks->addMidiTracks(
			m_pFiles->midiListView()->openFileNames(), iClipStart);
		m_pTracks->trackView()->ensureVisibleFrame(iClipStart);
	}
}


// Export tracks to audio file.
void qtractorMainForm::trackExportAudio (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackExportAudio()");
#endif

	qtractorExportForm exportForm(this);
	exportForm.setExportType(qtractorTrack::Audio);
	exportForm.exec();
}


// Export tracks to MIDI file.
void qtractorMainForm::trackExportMidi (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackExportMidi()");
#endif

	qtractorExportForm exportForm(this);
	exportForm.setExportType(qtractorTrack::Midi);
	exportForm.exec();
}


// Track automation curve selection menu.
Q_DECLARE_METATYPE(qtractorSubject *);

void qtractorMainForm::trackCurveSelect ( QAction *pAction )
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

	qtractorCurveList *pCurveList = pTrack->curveList();
	if (pCurveList == NULL)
		return;

	qtractorSubject *pSubject
		= qVariantValue<qtractorSubject *> (pAction->data());
	qtractorCurve *pCurve = NULL;
	if (pSubject) {
		pCurve = pSubject->curve();
		if (pCurve == NULL)
			pCurve = new qtractorCurve(pCurveList, pSubject, qtractorCurve::Hold);
	}

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackCurveSelect(%p)", pCurve);
#endif

	pCurveList->setCurrentCurve(pCurve);

	m_pTracks->updateTrackView();
}


void qtractorMainForm::trackCurveMode ( QAction *pAction )
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

	qtractorCurve *pCurrentCurve = pTrack->currentCurve();
	if (pCurrentCurve == NULL)
		return;

	qtractorCurve::Mode mode = qtractorCurve::Mode(pAction->data().toInt());

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackCurveMode(%d)", int(mode));
#endif

	pCurrentCurve->setMode(mode);
	pCurrentCurve->update();
	
	m_pTracks->updateTrackView();
}


// Track automation curve playback toggle.
void qtractorMainForm::trackCurveProcess ( bool bOn )
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

	qtractorCurve *pCurrentCurve = pTrack->currentCurve();
	if (pCurrentCurve == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackCurveProcess(%d)", int(bOn));
#endif

	if (!bOn) pCurrentCurve->setCapture(false);
	pCurrentCurve->setProcess(bOn);

	m_pTracks->updateTrackList();
}


// Track automation curve record toggle.
void qtractorMainForm::trackCurveCapture ( bool bOn )
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

	qtractorCurve *pCurrentCurve = pTrack->currentCurve();
	if (pCurrentCurve == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackCurveCapture(%d)", int(bOn));
#endif

	if (bOn) pCurrentCurve->setProcess(true);
	pCurrentCurve->setCapture(bOn);

	m_pTracks->updateTrackList();
}


// Track automation curve clear.
void qtractorMainForm::trackCurveClear (void)
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

	qtractorCurve *pCurrentCurve = pTrack->currentCurve();
	if (pCurrentCurve == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackCurveClear()");
#endif

	pCurrentCurve->clear();
	
	m_pTracks->updateTrackList();
	m_pTracks->updateTrackView();
}


// Track automation all curves playback toggle.
void qtractorMainForm::trackCurveProcessAll ( bool bOn )
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

	qtractorCurveList *pCurveList = pTrack->curveList();
	if (pCurveList == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackCurveProcessAll(%d)", int(bOn));
#endif

	pCurveList->setProcessAll(bOn);

	m_pTracks->updateTrackList();
}


// Track automation all curves record toggle.
void qtractorMainForm::trackCurveCaptureAll ( bool bOn )
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

	qtractorCurveList *pCurveList = pTrack->curveList();
	if (pCurveList == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackCurveCaptureAll(%d)", int(bOn));
#endif

	pCurveList->setCaptureAll(bOn);

	m_pTracks->updateTrackList();
}


// Track automation all curves clear.
void qtractorMainForm::trackCurveClearAll (void)
{
	qtractorTrack *pTrack = NULL;
	if (m_pTracks)
		pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return;

	qtractorCurveList *pCurveList = pTrack->curveList();
	if (pCurveList == NULL)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::trackCurveClearAll()");
#endif

	pCurveList->clearAll();
	
	m_pTracks->updateTrackList();
	m_pTracks->updateTrackView();
}


//-------------------------------------------------------------------------
// qtractorMainForm -- View Action slots.

// Show/hide the main program window menubar.
void qtractorMainForm::viewMenubar ( bool bOn )
{
#if 0
	if (bOn)
		m_ui.menuBar->show();
	else
		m_ui.menuBar->hide();
#else
	m_ui.menuBar->setVisible(bOn);
#endif
}


// Show/hide the main program window statusbar.
void qtractorMainForm::viewStatusbar ( bool bOn )
{
#if 0
	if (bOn)
		statusBar()->show();
	else
		statusBar()->hide();
#else
	statusBar()->setVisible(bOn);
#endif
}


// Show/hide the file-toolbar.
void qtractorMainForm::viewToolbarFile ( bool bOn )
{
#if 0
	if (bOn) {
		m_ui.fileToolbar->show();
	} else {
		m_ui.fileToolbar->hide();
	}
#else
	m_ui.fileToolbar->setVisible(bOn);
#endif
}


// Show/hide the edit-toolbar.
void qtractorMainForm::viewToolbarEdit ( bool bOn )
{
#if 0
	if (bOn) {
		m_ui.editToolbar->show();
	} else {
		m_ui.editToolbar->hide();
	}
#else
	m_ui.editToolbar->setVisible(bOn);
#endif
}


// Show/hide the track-toolbar.
void qtractorMainForm::viewToolbarTrack ( bool bOn )
{
#if 0
	if (bOn) {
		m_ui.trackToolbar->show();
	} else {
		m_ui.trackToolbar->hide();
	}
#else
	m_ui.trackToolbar->setVisible(bOn);
#endif
}


// Show/hide the view-toolbar.
void qtractorMainForm::viewToolbarView ( bool bOn )
{
#if 0
	if (bOn) {
		m_ui.viewToolbar->show();
	} else {
		m_ui.viewToolbar->hide();
	}
#else
	m_ui.viewToolbar->setVisible(bOn);
#endif
}


// Show/hide the options toolbar.
void qtractorMainForm::viewToolbarOptions ( bool bOn )
{
#if 0
	if (bOn) {
		m_ui.optionsToolbar->show();
	} else {
		m_ui.optionsToolbar->hide();
	}
#else
	m_ui.optionsToolbar->setVisible(bOn);
#endif
}


// Show/hide the transport toolbar.
void qtractorMainForm::viewToolbarTransport ( bool bOn )
{
#if 0
	if (bOn) {
		m_ui.transportToolbar->show();
	} else {
		m_ui.transportToolbar->hide();
	}
#else
	m_ui.transportToolbar->setVisible(bOn);
#endif
}


// Show/hide the time toolbar.
void qtractorMainForm::viewToolbarTime ( bool bOn )
{
#if 0
	if (bOn) {
		m_ui.timeToolbar->show();
	} else {
		m_ui.timeToolbar->hide();
	}
#else
	m_ui.timeToolbar->setVisible(bOn);
#endif
}


// Show/hide the thumb (track-line)ime toolbar.
void qtractorMainForm::viewToolbarThumb ( bool bOn )
{
#if 0
	if (bOn) {
		m_ui.thumbToolbar->show();
	} else {
		m_ui.thumbToolbar->hide();
	}
#else
	m_ui.thumbToolbar->setVisible(bOn);
#endif
}


// Show/hide the files window view.
void qtractorMainForm::viewFiles ( bool bOn )
{
#if 0
	if (bOn) {
		m_pFiles->show();
	} else {
		m_pFiles->hide();
	}
#else
	m_pFiles->setVisible(bOn);
#endif
}


// Show/hide the messages window logger.
void qtractorMainForm::viewMessages ( bool bOn )
{
#if 0
	if (bOn) {
		m_pMessages->show();
	} else {
		m_pMessages->hide();
	}
#else
	m_pMessages->setVisible(bOn);
#endif
}


// Show/hide the mixer window.
void qtractorMainForm::viewMixer ( bool bOn )
{
	if (m_pOptions)
		m_pOptions->saveWidgetGeometry(m_pMixer);
#if 0
	if (bOn) {
		m_pMixer->show();
	} else {
		m_pMixer->hide();
	}
#else
	m_pMixer->setVisible(bOn);
#endif
}


// Show/hide the connections window.
void qtractorMainForm::viewConnections ( bool bOn )
{
	if (m_pOptions)
		m_pOptions->saveWidgetGeometry(m_pConnections);
#if 0
	if (bOn) {
		m_pConnections->reset();
		m_pConnections->show();
	} else {
		m_pConnections->hide();
	}
#else
	if (bOn) m_pConnections->reset();
	m_pConnections->setVisible(bOn);
#endif
}


// Horizontal and/or vertical zoom-in.
void qtractorMainForm::viewZoomIn (void)
{
	if (m_pTracks)
		m_pTracks->zoomIn();
}


// Horizontal and/or vertical zoom-out.
void qtractorMainForm::viewZoomOut (void)
{
	if (m_pTracks)
		m_pTracks->zoomOut();
}


// Reset zoom level to default.
void qtractorMainForm::viewZoomReset (void)
{
	if (m_pTracks)
		m_pTracks->zoomReset();
}


// Set horizontal zoom mode
void qtractorMainForm::viewZoomHorizontal (void)
{
	if (m_pTracks)
		m_pTracks->setZoomMode(qtractorTracks::ZoomHorizontal);
}


// Set vertical zoom mode
void qtractorMainForm::viewZoomVertical (void)
{
	if (m_pTracks)
		m_pTracks->setZoomMode(qtractorTracks::ZoomVertical);
}


// Set all zoom mode
void qtractorMainForm::viewZoomAll (void)
{
	if (m_pTracks)
		m_pTracks->setZoomMode(qtractorTracks::ZoomAll);
}


// Set grid mode
void qtractorMainForm::viewSnapGrid ( bool bOn )
{
	if (m_pTracks)
		m_pTracks->trackView()->setSnapGrid(bOn);
}


// Set floating tool-tips view mode
void qtractorMainForm::viewToolTips ( bool bOn )
{
	if (m_pTracks)
		m_pTracks->trackView()->setToolTips(bOn);
}


// Change snap-per-beat setting via menu.
void qtractorMainForm::viewSnap (void)
{
	// Retrieve snap-per-beat index from from action data...
	QAction *pAction = qobject_cast<QAction *> (sender());
	if (pAction) {
		// Commit the change as usual...
		snapPerBeatChanged(pAction->data().toInt());
		// Update the other toolbar control...
		m_pSnapPerBeatComboBox->setCurrentIndex(
			qtractorTimeScale::indexFromSnap(m_pSession->snapPerBeat()));
	}
}


// Refresh view display.
void qtractorMainForm::viewRefresh (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::viewRefresh()");
#endif

	// Tell the world we'll take some time...
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

	// Update the whole session view dependables...
	m_pSession->updateTimeScale();
	m_pSession->updateSessionLength();

	if (m_pTracks)
		m_pTracks->updateContents(true);
	if (m_pConnections)
		m_pConnections->refresh();
	if (m_pMixer) {
		m_pMixer->updateBuses();
		m_pMixer->updateTracks();
	}

	m_pThumbView->updateContents();

	// Update other editors contents...
	QListIterator<qtractorMidiEditorForm *> iter(m_editors);
	while (iter.hasNext()) {
		qtractorMidiEditor *pEditor = (iter.next())->editor();
		pEditor->updateTimeScale();
		pEditor->updateContents();
	}

	// We're formerly done.
	QApplication::restoreOverrideCursor();

	stabilizeForm();
}


// Show instruments dialog.
void qtractorMainForm::viewInstruments (void)
{
	// Just set and show the instruments dialog...
	qtractorInstrumentForm(this).exec();
}


// Show MIDI controllers dialog.
void qtractorMainForm::viewControllers (void)
{
	// Just set and show the MIDI controllers dialog...
	qtractorMidiControlForm(this).exec();
}


// Show buses dialog.
void qtractorMainForm::viewBuses (void)
{
	// Just set and show the buses dialog...
	qtractorBusForm(this).exec();
}


// Show tempo-map dialog.
void qtractorMainForm::viewTempoMap (void)
{
	// Just set and show the tempo-map dialog...
	qtractorTimeScaleForm(this).exec();
}


// Show options dialog.
void qtractorMainForm::viewOptions (void)
{
	if (m_pOptions == NULL)
		return;

#ifdef CONFIG_LV2
#if defined(__WIN32__) || defined(_WIN32) || defined(WIN32)
	const QString sPathSep(';');
#else
	const QString sPathSep(':');
#endif
	QString sOldLv2Path = m_pOptions->lv2Paths.join(sPathSep);
#endif

	// Check out some initial nullities(tm)...
	if (m_pOptions->sMessagesFont.isEmpty() && m_pMessages)
		m_pOptions->sMessagesFont = m_pMessages->messagesFont().toString();
	// To track down deferred or immediate changes.
	bool    bOldMessagesLog        = m_pOptions->bMessagesLog; 
	QString sOldMessagesLogPath    = m_pOptions->sMessagesLogPath;
	QString sOldMessagesFont       = m_pOptions->sMessagesFont;
	bool    bOldStdoutCapture      = m_pOptions->bStdoutCapture;
	int     bOldMessagesLimit      = m_pOptions->bMessagesLimit;
	int     iOldMessagesLimitLines = m_pOptions->iMessagesLimitLines;
	bool    bOldCompletePath       = m_pOptions->bCompletePath;
	bool    bOldPeakAutoRemove     = m_pOptions->bPeakAutoRemove;
	bool    bOldKeepToolsOnTop     = m_pOptions->bKeepToolsOnTop;
	int     iOldMaxRecentFiles     = m_pOptions->iMaxRecentFiles;
	int     iOldDisplayFormat      = m_pOptions->iDisplayFormat;
	int     iOldBaseFontSize       = m_pOptions->iBaseFontSize;
	int     iOldResampleType       = m_pOptions->iAudioResampleType;
	bool    bOldWsolaTimeStretch   = m_pOptions->bAudioWsolaTimeStretch;
	bool    bOldWsolaQuickSeek     = m_pOptions->bAudioWsolaQuickSeek;
	bool    bOldAudioPlayerBus     = m_pOptions->bAudioPlayerBus;
	bool    bOldAudioMetronome     = m_pOptions->bAudioMetronome;
	int     iOldTransportMode      = m_pOptions->iTransportMode;
	int     iOldMidiMmcDevice      = m_pOptions->iMidiMmcDevice;
	int     iOldMidiMmcMode        = m_pOptions->iMidiMmcMode;
	int     iOldMidiSppMode        = m_pOptions->iMidiSppMode;
	int     iOldMidiClockMode      = m_pOptions->iMidiClockMode;
	int     iOldMidiCaptureQuantize = m_pOptions->iMidiCaptureQuantize;
	int     iOldMidiQueueTimer     = m_pOptions->iMidiQueueTimer;
	QString sOldMetroBarFilename   = m_pOptions->sMetroBarFilename;
	QString sOldMetroBeatFilename  = m_pOptions->sMetroBeatFilename;
	float   fOldMetroBarGain       = m_pOptions->fMetroBarGain;
	float   fOldMetroBeatGain      = m_pOptions->fMetroBeatGain;
	bool    bOldAudioMetroBus      = m_pOptions->bAudioMetroBus;
	bool    bOldMidiControlBus     = m_pOptions->bMidiControlBus;
	bool    bOldMidiMetronome      = m_pOptions->bMidiMetronome;
	int     iOldMetroChannel       = m_pOptions->iMetroChannel;
	int     iOldMetroBarNote       = m_pOptions->iMetroBarNote;
	int     iOldMetroBarVelocity   = m_pOptions->iMetroBarVelocity;
	int     iOldMetroBarDuration   = m_pOptions->iMetroBarDuration;
	int     iOldMetroBeatNote      = m_pOptions->iMetroBeatNote;
	int     iOldMetroBeatVelocity  = m_pOptions->iMetroBeatVelocity;
	int     iOldMetroBeatDuration  = m_pOptions->iMetroBeatDuration;
	bool    bOldMidiMetroBus       = m_pOptions->bMidiMetroBus;
	// Load the current setup settings.
	qtractorOptionsForm optionsForm(this);
	optionsForm.setOptions(m_pOptions);
	// Show the setup dialog...
	if (optionsForm.exec()) {
		enum { RestartSession = 1, RestartProgram = 2, RestartAny = 3 };
		int iNeedRestart = 0;
		// Check wheather something immediate has changed.
		if (iOldResampleType != m_pOptions->iAudioResampleType) {
			qtractorAudioBuffer::setResampleType(m_pOptions->iAudioResampleType);
			iNeedRestart |= RestartSession;
		}
		if (( bOldWsolaTimeStretch && !m_pOptions->bAudioWsolaTimeStretch) ||
			(!bOldWsolaTimeStretch &&  m_pOptions->bAudioWsolaTimeStretch)) {
			qtractorAudioBuffer::setWsolaTimeStretch(
				m_pOptions->bAudioWsolaTimeStretch);
			iNeedRestart |= RestartSession;
		}
		// Audio engine control modes...
		if (iOldTransportMode != m_pOptions->iTransportMode) {
			++m_iDirtyCount; // Fake session properties change.
			updateTransportMode();
		//	iNeedRestart |= RestartSession;
		}
		if (iOldMidiQueueTimer != m_pOptions->iMidiQueueTimer) {
			updateMidiQueueTimer();
			iNeedRestart |= RestartSession;
		}
		if (( bOldWsolaQuickSeek && !m_pOptions->bAudioWsolaQuickSeek) ||
			(!bOldWsolaQuickSeek &&  m_pOptions->bAudioWsolaQuickSeek)) {
			qtractorAudioBuffer::setWsolaQuickSeek(
				m_pOptions->bAudioWsolaQuickSeek);
			iNeedRestart |= RestartSession;
		}
	#ifdef CONFIG_LV2
		if (sOldLv2Path != m_pOptions->lv2Paths.join(sPathSep)) {
			updatePluginPaths();
			iNeedRestart |= RestartSession;
		}
	#endif
		if (( bOldStdoutCapture && !m_pOptions->bStdoutCapture) ||
			(!bOldStdoutCapture &&  m_pOptions->bStdoutCapture)) {
			updateMessagesCapture();
			iNeedRestart |= RestartProgram;
		}
		if (iOldBaseFontSize != m_pOptions->iBaseFontSize)
			iNeedRestart |= RestartProgram;
		if (( bOldCompletePath && !m_pOptions->bCompletePath) ||
			(!bOldCompletePath &&  m_pOptions->bCompletePath) ||
			(iOldMaxRecentFiles != m_pOptions->iMaxRecentFiles))
			updateRecentFilesMenu();
		if (( bOldPeakAutoRemove && !m_pOptions->bPeakAutoRemove) ||
			(!bOldPeakAutoRemove &&  m_pOptions->bPeakAutoRemove))
			updatePeakAutoRemove();
		if (( bOldKeepToolsOnTop && !m_pOptions->bKeepToolsOnTop) ||
			(!bOldKeepToolsOnTop &&  m_pOptions->bKeepToolsOnTop))
			iNeedRestart |= RestartProgram;
		if (sOldMessagesFont != m_pOptions->sMessagesFont)
			updateMessagesFont();
		if (( bOldMessagesLimit && !m_pOptions->bMessagesLimit) ||
			(!bOldMessagesLimit &&  m_pOptions->bMessagesLimit) ||
			(iOldMessagesLimitLines !=  m_pOptions->iMessagesLimitLines))
			updateMessagesLimit();
		if (iOldDisplayFormat != m_pOptions->iDisplayFormat)
			updateDisplayFormat();
		if (( bOldMessagesLog && !m_pOptions->bMessagesLog) ||
			(!bOldMessagesLog &&  m_pOptions->bMessagesLog) ||
			(sOldMessagesLogPath != m_pOptions->sMessagesLogPath))
			m_pMessages->setLogging(
				m_pOptions->bMessagesLog, m_pOptions->sMessagesLogPath);
		// FIXME: This is what it should ever be,
		// make it right from this very moment...
		qtractorAudioFileFactory::setDefaultType(
			m_pOptions->sAudioCaptureExt,
			m_pOptions->iAudioCaptureType,
			m_pOptions->iAudioCaptureFormat,
			m_pOptions->iAudioCaptureQuality);
		qtractorMidiClip::setDefaultFormat(
			m_pOptions->iMidiCaptureFormat);
		// Set default MIDI (plugin) instrument audio output mode.
		qtractorMidiManager::setDefaultAudioOutputBus(
			m_pOptions->bAudioOutputBus);
		// Auto time-stretching global mode...
		if (m_pSession)
			m_pSession->setAutoTimeStretch(m_pOptions->bAudioAutoTimeStretch);
		// Special track-view drop-span mode...
		if (m_pTracks)
			m_pTracks->trackView()->setDropSpan(m_pOptions->bTrackViewDropSpan);
		// MIDI engine control modes...
		if ((iOldMidiMmcDevice != m_pOptions->iMidiMmcDevice) ||
			(iOldMidiMmcMode   != m_pOptions->iMidiMmcMode)   ||
			(iOldMidiSppMode   != m_pOptions->iMidiSppMode)   ||
			(iOldMidiClockMode != m_pOptions->iMidiClockMode) ||
			(iOldMidiCaptureQuantize != m_pOptions->iMidiCaptureQuantize)) {
			++m_iDirtyCount; // Fake session properties change.
			updateMidiControlModes();
		}
		// Audio engine audition/pre-listening player options...
		if (( bOldAudioPlayerBus && !m_pOptions->bAudioPlayerBus) ||
			(!bOldAudioPlayerBus &&  m_pOptions->bAudioPlayerBus))
			updateAudioPlayer();
		// MIDI engine control options...
		if (( bOldMidiControlBus && !m_pOptions->bMidiControlBus) ||
			(!bOldMidiControlBus &&  m_pOptions->bMidiControlBus))
			updateMidiControl();
		// Audio engine metronome options...
		if (( bOldAudioMetronome   && !m_pOptions->bAudioMetronome)   ||
			(!bOldAudioMetronome   &&  m_pOptions->bAudioMetronome)   ||
			(sOldMetroBarFilename  != m_pOptions->sMetroBarFilename)  ||
			(sOldMetroBeatFilename != m_pOptions->sMetroBeatFilename) ||
			(fOldMetroBarGain      != m_pOptions->fMetroBarGain)      ||
			(fOldMetroBeatGain     != m_pOptions->fMetroBeatGain)     ||
			( bOldAudioMetroBus    && !m_pOptions->bAudioMetroBus)    ||
			(!bOldAudioMetroBus    &&  m_pOptions->bAudioMetroBus))
			updateAudioMetronome();
		// MIDI engine metronome options...
		if (( bOldMidiMetronome    && !m_pOptions->bMidiMetronome)    ||
			(!bOldMidiMetronome    &&  m_pOptions->bMidiMetronome)    ||
			(iOldMetroChannel      != m_pOptions->iMetroChannel)      ||
			(iOldMetroBarNote      != m_pOptions->iMetroBarNote)      ||
			(iOldMetroBarVelocity  != m_pOptions->iMetroBarVelocity)  ||
			(iOldMetroBarDuration  != m_pOptions->iMetroBarDuration)  ||
			(iOldMetroBeatNote     != m_pOptions->iMetroBeatNote)     ||
			(iOldMetroBeatVelocity != m_pOptions->iMetroBeatVelocity) ||
			(iOldMetroBeatDuration != m_pOptions->iMetroBeatDuration) ||
			( bOldMidiMetroBus     && !m_pOptions->bMidiMetroBus)     ||
			(!bOldMidiMetroBus     &&  m_pOptions->bMidiMetroBus))
			updateMidiMetronome();
		// Warn if something will be only effective on next time.
		if (iNeedRestart & RestartAny) {
			QString sNeedRestart;
			if (iNeedRestart & RestartSession)
				sNeedRestart += tr("session");
			if (iNeedRestart & RestartProgram) {
				if (!sNeedRestart.isEmpty())
					sNeedRestart += tr(" or ");
				sNeedRestart += tr("program");
			}
			// Show restart needed message...
			QMessageBox::information(this,
				tr("Information") + " - " QTRACTOR_TITLE,
				tr("Some settings may be only effective\n"
				"next time you start this %1.")
				.arg(sNeedRestart));
		}
	}

	// This makes it.
	stabilizeForm();
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Transport Action slots.

// Transport backward.
void qtractorMainForm::transportBackward (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportBackward()");
#endif

	// Make sure session is activated...
	checkRestartSession();

	// Move playhead to edit-tail, head or full session-start.
	if (QApplication::keyboardModifiers()
		& (Qt::ShiftModifier | Qt::ControlModifier)) {
		m_pSession->setPlayHead(0);
	} else {
		unsigned long iPlayHead = m_pSession->playHead();
	#if 0
		if (iPlayHead > m_pSession->editTail() && !m_pSession->isPlaying())
			iPlayHead = m_pSession->editTail();
		else
		if (iPlayHead > m_pSession->editHead())
			iPlayHead = m_pSession->editHead();
		else
			iPlayHead = 0;
	#else
		QList<unsigned long> list;
		list.append(0);
		if (iPlayHead > m_pSession->editHead())
			list.append(m_pSession->editHead());
	//	if (iPlayHead > m_pSession->editTail() && !m_pSession->isPlaying())
	//		list.append(m_pSession->editTail());
		if (m_pSession->isLooping()) {
			if (iPlayHead > m_pSession->loopStart())
				list.append(m_pSession->loopStart());
		//	if (iPlayHead > m_pSession->loopEnd() && !m_pSession->isPlaying())
		//		list.append(m_pSession->loopEnd());
		}
		if (iPlayHead > m_pSession->sessionLength() && !m_pSession->isPlaying())
			list.append(m_pSession->sessionLength());
		qSort(list.begin(), list.end());
		iPlayHead = list.last();
	#endif
		m_pSession->setPlayHead(iPlayHead);
	}
	++m_iTransportUpdate;

	stabilizeForm();
}


// Transport rewind.
void qtractorMainForm::transportRewind (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportRewind()");
#endif

	// Make sure session is activated...
	if (!checkRestartSession())
		return;

	// Rolling direction and speed (negative)...
	int iRolling = -1;
	if (QApplication::keyboardModifiers()
		& (Qt::ShiftModifier | Qt::ControlModifier))
		iRolling -= 2;

	// Toggle rolling backward...
	if (setRolling(iRolling) < 0) {
		// Send MMC STOP command...
		m_pSession->midiEngine()->sendMmcCommand(
			qtractorMmcEvent::STOP);
	} else {
		// Send MMC REWIND command...
		m_pSession->midiEngine()->sendMmcCommand(
			qtractorMmcEvent::REWIND);
	}

	stabilizeForm();
}


// Transport fast-forward
void qtractorMainForm::transportFastForward (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportFastForward()");
#endif

	// Make sure session is activated...
	if (!checkRestartSession())
		return;

	// Rolling direction and speed (positive)...
	int iRolling = +1;
	if (QApplication::keyboardModifiers()
		& (Qt::ShiftModifier | Qt::ControlModifier))
		iRolling += 2;

	// Toggle rolling backward...
	if (setRolling(iRolling) > 0) {
		// Send MMC STOP command...
		m_pSession->midiEngine()->sendMmcCommand(
			qtractorMmcEvent::STOP);
	} else {
		// Send MMC FAST_FORWARD command...
		m_pSession->midiEngine()->sendMmcCommand(
			qtractorMmcEvent::FAST_FORWARD);
	}

	stabilizeForm();
}


// Transport forward
void qtractorMainForm::transportForward (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportForward()");
#endif

	// Make sure session is activated...
	checkRestartSession();

	// Move playhead to edit-head, tail or full session-end.
	if (QApplication::keyboardModifiers()
		& (Qt::ShiftModifier | Qt::ControlModifier)) {
		m_pSession->setPlayHead(m_pSession->sessionLength());
	} else {
		unsigned long iPlayHead = m_pSession->playHead();
	#if 0
		if (iPlayHead < m_pSession->editHead())
			iPlayHead = m_pSession->editHead();
		else
		if (iPlayHead < m_pSession->editTail())
			iPlayHead = m_pSession->editTail();
		else
			iPlayHead = m_pSession->sessionLength();
	#else
		QList<unsigned long> list;
		if (iPlayHead < m_pSession->editHead())
			list.append(m_pSession->editHead());
		if (iPlayHead < m_pSession->editTail())
			list.append(m_pSession->editTail());
		if (m_pSession->isLooping()) {
			if (iPlayHead < m_pSession->loopStart())
				list.append(m_pSession->loopStart());
			if (iPlayHead < m_pSession->loopEnd())
				list.append(m_pSession->loopEnd());
		}
		if (iPlayHead < m_pSession->sessionLength())
			list.append(m_pSession->sessionLength());
		qSort(list.begin(), list.end());
		iPlayHead = list.first();
	#endif
		m_pSession->setPlayHead(iPlayHead);
	}
	++m_iTransportUpdate;

	stabilizeForm();
}


// Transport loop.
void qtractorMainForm::transportLoop (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportLoop()");
#endif

	// Make sure session is activated...
	checkRestartSession();

	// Do the loop toggle switch...
	unsigned long iLoopStart = 0;
	unsigned long iLoopEnd   = 0;

	if (!m_pSession->isLooping()) {
		iLoopStart = m_pSession->editHead();
		iLoopEnd   = m_pSession->editTail();
	}

	// Now, express the change as an undoable command...
	m_pSession->execute(
		new qtractorSessionLoopCommand(m_pSession, iLoopStart, iLoopEnd));
}


// Transport loop setting.
void qtractorMainForm::transportLoopSet (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportLoopSet()");
#endif

	// Make sure session is activated...
	checkRestartSession();

	// Now, express the change as an undoable command...
	m_pSession->execute(
		new qtractorSessionLoopCommand(m_pSession,
			m_pSession->editHead(), m_pSession->editTail()));
}


// Transport stop.
void qtractorMainForm::transportStop (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportStop()");
#endif

	// Make sure session is activated...
	if (!checkRestartSession())
		return;

	// Stop playing...
	if (setPlaying(false)) {
		qtractorMidiEngine *pMidiEngine = m_pSession->midiEngine();
		if (pMidiEngine) {
			// Send MMC PLAY/STOP command...
			pMidiEngine->sendMmcCommand(qtractorMmcEvent::STOP);
			pMidiEngine->sendSppCommand(SND_SEQ_EVENT_STOP);
		}
	}

	stabilizeForm();
}


// Transport play.
void qtractorMainForm::transportPlay (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportPlay()");
#endif

	// Make sure session is activated...
	if (!checkRestartSession())
		return;

	// Toggle playing...
	bool bPlaying = !m_pSession->isPlaying();
	if (setPlaying(bPlaying)) {
		qtractorMidiEngine *pMidiEngine = m_pSession->midiEngine();
		if (pMidiEngine) {
			// Send MMC PLAY/STOP command...
			pMidiEngine->sendMmcCommand(bPlaying
				? qtractorMmcEvent::PLAY
				: qtractorMmcEvent::STOP);
			pMidiEngine->sendSppCommand(bPlaying
				? (m_pSession->playHead() > 0
					? SND_SEQ_EVENT_CONTINUE
					: SND_SEQ_EVENT_START)
				: SND_SEQ_EVENT_STOP);
		}
	}

	stabilizeForm();
}


// Transport record.
void qtractorMainForm::transportRecord (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportRecord()");
#endif

	// Make sure session is activated...
	if (!checkRestartSession())
		return;

	// Toggle recording...
	bool bRecording = !m_pSession->isRecording();
	if (setRecording(bRecording)) {
		// Send MMC RECORD_STROBE/EXIT command...
		m_pSession->midiEngine()->sendMmcCommand(bRecording ?
			qtractorMmcEvent::RECORD_STROBE : qtractorMmcEvent::RECORD_EXIT);
	}

	stabilizeForm();
}


// Transport punch in/out.
void qtractorMainForm::transportPunch (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportPunch()");
#endif

	// Make sure session is activated...
	checkRestartSession();

	// Do the punch in/out toggle switch...
	unsigned long iPunchIn  = 0;
	unsigned long iPunchOut = 0;

	if (!m_pSession->isPunching()) {
		iPunchIn  = m_pSession->editHead();
		iPunchOut = m_pSession->editTail();
	}

	// Now, express the change as an undoable command...
	m_pSession->execute(
		new qtractorSessionPunchCommand(m_pSession, iPunchIn, iPunchOut));
}


// Transport punch set.
void qtractorMainForm::transportPunchSet (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportPunchSet()");
#endif

	// Make sure session is activated...
	checkRestartSession();

	// Now, express the change as an undoable command...
	m_pSession->execute(
		new qtractorSessionPunchCommand(m_pSession,
			m_pSession->editHead(), m_pSession->editTail()));
}


// Metronome transport option.
void qtractorMainForm::transportMetro (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportMetro()");
#endif

	// Toggle Audio metronome...
	if (m_pOptions->bAudioMetronome) {
		qtractorAudioEngine *pAudioEngine = m_pSession->audioEngine();
		if (pAudioEngine)
			pAudioEngine->setMetronome(!pAudioEngine->isMetronome());
	}

	// Toggle MIDI metronome...
	if (m_pOptions->bMidiMetronome) {
		qtractorMidiEngine *pMidiEngine = m_pSession->midiEngine();
		if (pMidiEngine)
			pMidiEngine->setMetronome(!pMidiEngine->isMetronome());
	}

	stabilizeForm();
}


// Follow playhead transport option.
void qtractorMainForm::transportFollow (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportFollow()");
#endif

	// Toggle follow-playhead...
	stabilizeForm();
}


// Auto-backward transport option.
void qtractorMainForm::transportAutoBackward (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportAutoBackward()");
#endif

	// Toggle auto-backward...
	stabilizeForm();
}


// Continue past end transport option.
void qtractorMainForm::transportContinue (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportContinue()");
#endif

	// Toggle continue-past-end...
	stabilizeForm();
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Help Action slots.

// Show (and edit) keyboard shortcuts.
void qtractorMainForm::helpShortcuts (void)
{
	if (m_pOptions == NULL)
		return;

	qtractorShortcutForm shortcutForm(findChildren<QAction *> (), this);
	if (shortcutForm.exec())
		m_pOptions->saveActionShortcuts(this);
}


// Show information about application program.
void qtractorMainForm::helpAbout (void)
{
	QStringList list;
#ifdef CONFIG_DEBUG
	list << tr("Debugging option enabled.");
#endif
#ifndef CONFIG_LIBVORBIS
	list << tr("Ogg Vorbis (libvorbis) file support disabled.");
#endif
#ifndef CONFIG_LIBMAD
	list << tr("MPEG-1 Audio Layer 3 (libmad) file support disabled.");
#endif
#ifndef CONFIG_LIBSAMPLERATE
	list << tr("Sample-rate conversion (libsamplerate) disabled.");
#endif
#ifndef CONFIG_LIBRUBBERBAND
	list << tr("Pitch-shifting support (librubberband) disabled.");
#endif
#ifndef CONFIG_LIBLO
	list << tr("OSC service support (liblo) disabled.");
#endif
#ifndef CONFIG_LADSPA
	list << tr("LADSPA Plug-in support disabled.");
#endif
#ifndef CONFIG_DSSI
	list << tr("DSSI Plug-in support disabled.");
#endif
#ifndef CONFIG_VST
	list << tr("VST Plug-in support disabled.");
#endif
#ifdef  CONFIG_VESTIGE
	list << tr("VeSTige header support enabled.");
#endif
#ifndef CONFIG_LV2
	list << tr("LV2 Plug-in support disabled.");
#else
#ifndef CONFIG_LV2_EVENT
	list << tr("LV2 Plug-in MIDI/Event support disabled.");
#endif
#ifdef  CONFIG_LIBLILV
	list << tr("LV2 Plug-in support (liblilv) enabled. (NEW)");
#endif
#ifdef  CONFIG_LIBSUIL
	list << tr("LV2 Plug-in UI instantiation support (libsuil) enabled. (NEW)");
#ifndef CONFIG_LV2_QT4_UI
	list << tr("LV2 Plug-in QT4 UI support disabled.");
#endif
#endif
#ifndef CONFIG_LV2_GTK_UI
	list << tr("LV2 Plug-in GTK UI support disabled.");
#endif
#ifndef CONFIG_LV2_EXTERNAL_UI
	list << tr("LV2 Plug-in External UI support disabled.");
#endif
#ifndef CONFIG_LV2_SAVERESTORE
	list << tr("LV2 Plug-in Save/Restore support disabled.");
#endif
#ifndef CONFIG_LV2_PERSIST
	list << tr("LV2 Plug-in Persist support disabled.");
#endif
#endif // CONFIG_LV2
#ifndef CONFIG_JACK_SESSION
	list << tr("JACK Session support disabled.");
#endif
#ifndef CONFIG_JACK_LATENCY
	list << tr("JACK Latency support disabled.");
#endif

	// Stuff the about box text...
	QString sText = "<p>\n";
	sText += "<b>" QTRACTOR_TITLE " - " + tr(QTRACTOR_SUBTITLE) + "</b><br />\n";
	sText += "<br />\n";
	sText += tr("Version") + ": <b>" QTRACTOR_VERSION "</b><br />\n";
	sText += "<small>" + tr("Build") + ": " __DATE__ " " __TIME__ "</small><br />\n";
	QStringListIterator iter(list);
	while (iter.hasNext()) {
		sText += "<small><font color=\"red\">";
		sText += iter.next();
		sText += "</font></small><br />";
	}
	sText += "<br />\n";
	sText += tr("Website") + ": <a href=\"" QTRACTOR_WEBSITE "\">" QTRACTOR_WEBSITE "</a><br />\n";
	sText += "<br />\n";
	sText += "<small>";
	sText += QTRACTOR_COPYRIGHT "<br />\n";
	sText += "<br />\n";
	sText += tr("This program is free software; you can redistribute it and/or modify it") + "<br />\n";
	sText += tr("under the terms of the GNU General Public License version 2 or later.");
	sText += "</small>";
	sText += "</p>\n";

	QMessageBox::about(this, tr("About") + " " QTRACTOR_TITLE, sText);
}


// Show information about the Qt toolkit.
void qtractorMainForm::helpAboutQt (void)
{
	QMessageBox::aboutQt(this);
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Internal transport stabilization.

bool qtractorMainForm::setPlaying ( bool bPlaying )
{
	// In case of (re)starting playback, send now
	// all tracks MIDI bank select/program changes...
	if (bPlaying)
		m_pSession->setMidiPatch(true); // Force conditional!

	// Toggle engine play status...
	m_pSession->setPlaying(bPlaying);

	// We must start/stop certain things...
	if (!bPlaying) {
		// Shutdown recording anyway...
		if (m_pSession->isRecording())
			setRecording(false);
		// Stop transport rolling, immediately...
		setRolling(0);
		// Auto-backward reset feature...
		if (m_ui.transportAutoBackwardAction->isChecked()) {
			unsigned long iPlayHead = m_pSession->playHead();
			if (iPlayHead > m_pSession->editHead())
				iPlayHead = m_pSession->editHead();
			else
				iPlayHead = 0;
			m_pSession->setPlayHead(iPlayHead);
		}
	}	// Start something... ;)
	else ++m_iTransportUpdate;

	// Done with playback switch...
	return true;
}


bool qtractorMainForm::setRecording ( bool bRecording )
{
	// Avoid if no tracks are armed...
	if (m_pSession->recordTracks() < 1)
		return false;

	if (bRecording) {
		// Starting recording: we must have a session name...
		if (m_pSession->sessionName().isEmpty() && !editSession())
			return false;
		// Will start recording...
	} else {
		// Stopping recording: fetch and commit
		// all new clips as a composite command...
		int iUpdate = 0;
		qtractorClipCommand *pClipCommand
			= new qtractorClipCommand(tr("record clip"));
		// Arrange for formal clip length...
		unsigned long iClipEnd = (m_pSession->isPunching()
			? m_pSession->punchOut() : m_pSession->framePos());
		// For all non-empty clip on record...
		for (qtractorTrack *pTrack = m_pSession->tracks().first();
				pTrack; pTrack = pTrack->next()) {
			if (pClipCommand->addClipRecord(pTrack, iClipEnd))
				++iUpdate;
		}
		// Put it in the form of an undoable command...
		if (iUpdate > 0) {
			m_pSession->execute(pClipCommand);
		} else {
			// The allocated command is unhelpful...
			delete pClipCommand;
			// Try to postpone an overall refresh...
			if (m_iPeakTimer  < QTRACTOR_TIMER_DELAY)
				m_iPeakTimer += QTRACTOR_TIMER_DELAY;
		}
	}

	// Finally, toggle session record status...
	m_pSession->setRecording(bRecording);

	// Done with record switch...
	return true;
}


int qtractorMainForm::setRolling ( int iRolling )
{
	int iOldRolling = m_iTransportRolling;

	// Avoid if recording is armed...
	if (m_pSession->isRecording() || iOldRolling == iRolling)
		iRolling = 0;

	// Set the rolling flag.
	m_iTransportRolling = iRolling;
	m_fTransportShuttle = float(iRolling);
	m_iTransportStep    = 0;

	// We've started/stopped something...
	if (m_iTransportRolling) {
		if (!m_bTransportPlaying)
			m_bTransportPlaying = m_pSession->isPlaying();
		if (m_bTransportPlaying)
			m_pSession->setPlaying(false);
		++m_iTransportUpdate;
	} else {
		if (m_bTransportPlaying)
			m_pSession->setPlaying(true);
		m_bTransportPlaying = false;
	}

	// Done with rolling switch...
	return iOldRolling;
}


void qtractorMainForm::setLocate ( unsigned long iLocate )
{
	m_pSession->setPlayHead(m_pSession->frameFromLocate(iLocate));
	++m_iTransportUpdate;
}


void qtractorMainForm::setShuttle ( float fShuttle )
{
	float fOldShuttle = m_fTransportShuttle;

	if (fShuttle < 0.0f && fOldShuttle >= 0.0f)
		setRolling(-1);
	else
	if (fShuttle > 0.0f && 0.0f >= fOldShuttle)
		setRolling(+1);

	m_fTransportShuttle = fShuttle;
	++m_iTransportUpdate;
}


void qtractorMainForm::setStep ( int iStep )
{
	m_iTransportStep += iStep;
	++m_iTransportUpdate;
}


void qtractorMainForm::setTrack ( int scmd, int iTrack, bool bOn )
{
	if (m_pTracks) {
		// Find which ordinal track...
		qtractorTrack *pTrack = m_pTracks->trackList()->track(iTrack);
		if (pTrack) {
			// Set session track mode state...
			switch (qtractorMmcEvent::SubCommand(scmd)) {
			case qtractorMmcEvent::TRACK_RECORD:
				pTrack->setRecord(bOn);
				break;
			case qtractorMmcEvent::TRACK_MUTE:
				pTrack->setMute(bOn);
				break;
			case qtractorMmcEvent::TRACK_SOLO:
				pTrack->setSolo(bOn);
				break;
			default:
				break;
			}
			// Done.
			stabilizeForm();
		}
	}
}


void qtractorMainForm::setSongPos ( unsigned short iSongPos )
{
	m_pSession->setPlayHead(m_pSession->frameFromSongPos(iSongPos));
	++m_iTransportUpdate;
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Main window stabilization.

void qtractorMainForm::updateTransportTime ( unsigned long iPlayHead )
{
	m_pTimeSpinBox->setValue(iPlayHead, false);
	m_pThumbView->updatePlayHead(iPlayHead);

	// Tricky stuff: node's non-null iif tempo changes...
	qtractorTimeScale::Node *pNode = m_pTempoCursor->seek(m_pSession, iPlayHead);
	if (pNode) {
		m_pTempoSpinBox->setTempo(pNode->tempo, false);
		m_pTempoSpinBox->setBeatsPerBar(pNode->beatsPerBar, false);
		m_pTempoSpinBox->setBeatDivisor(pNode->beatDivisor, false);
	}

#ifdef CONFIG_VST
#if 0 // !VST_FORCE_DEPRECATED
	qtractorVstPlugin::idleTimerAll();
#endif
#endif
}


void qtractorMainForm::stabilizeForm (void)
{
#ifdef CONFIG_DEBUG_0
	qDebug("qtractorMainForm::stabilizeForm()");
#endif

	// Update the main application caption...
	QString sSessionName = sessionName(m_sFilename);
	if (m_iDirtyCount > 0)
		sSessionName += ' ' + tr("[modified]");
	setWindowTitle(sSessionName + " - " QTRACTOR_TITLE);

	// Update the main menu state...
	m_ui.fileSaveAction->setEnabled(m_iDirtyCount > 0);

	// Update edit menu state...
	qtractorCommandList *pCommands = m_pSession->commands();
	pCommands->updateAction(m_ui.editUndoAction, pCommands->lastCommand());
	pCommands->updateAction(m_ui.editRedoAction, pCommands->nextCommand());

	unsigned long iPlayHead = m_pSession->playHead();
	unsigned long iSessionLength = m_pSession->sessionLength();

	qtractorTrack *pTrack = NULL;
	qtractorClip  *pClip  = NULL;
	bool bTracks = (m_pTracks && m_pSession->tracks().count() > 0);
	if (bTracks) {
		pTrack = m_pTracks->currentTrack();
		pClip  = m_pTracks->currentClip();
	}

	bool bEnabled    = (pTrack != NULL);
	bool bSelected   = (m_pTracks && m_pTracks->isClipSelected());
	bool bSelectable = (m_pSession->editHead() < m_pSession->editTail());
	bool bPlaying    = m_pSession->isPlaying();
	bool bRecording  = m_pSession->isRecording();
	bool bPunching   = m_pSession->isPunching();
	bool bLooping    = m_pSession->isLooping();
	bool bRolling    = (bPlaying && bRecording);
	bool bBumped     = (!bRolling && (iPlayHead > 0 || bPlaying));

	if (m_pFiles && m_pFiles->hasFocus() && m_pFiles->isFileSelected())
		bSelected = true;

	m_ui.editCutAction->setEnabled(bSelected);
	m_ui.editCopyAction->setEnabled(bSelected);
	m_ui.editPasteAction->setEnabled(qtractorTrackView::isClipboard()
		|| QApplication::clipboard()->mimeData()->hasUrls());
	m_ui.editPasteRepeatAction->setEnabled(qtractorTrackView::isClipboard());
	m_ui.editDeleteAction->setEnabled(bSelected);

	m_ui.editSelectAllAction->setEnabled(iSessionLength > 0);
	m_ui.editSelectInvertAction->setEnabled(iSessionLength > 0);
	m_ui.editSelectTrackRangeAction->setEnabled(bEnabled && bSelectable);
	m_ui.editSelectTrackAction->setEnabled(bEnabled);
	m_ui.editSelectRangeAction->setEnabled(iSessionLength > 0 && bSelectable);
	m_ui.editSelectNoneAction->setEnabled(bSelected);

	m_ui.editClipNewAction->setEnabled(bEnabled);
	m_ui.editClipEditAction->setEnabled(pClip != NULL);
	
	// Update track menu state...
	m_ui.trackRemoveAction->setEnabled(
		bEnabled && (!bRolling || !pTrack->isRecord()));
	m_ui.trackPropertiesAction->setEnabled(
		bEnabled && (!bRolling || !pTrack->isRecord()));
	m_ui.trackInputsAction->setEnabled(
		bEnabled && pTrack->inputBus() != NULL);
	m_ui.trackOutputsAction->setEnabled(
		bEnabled && pTrack->outputBus() != NULL);
	m_ui.trackStateMenu->setEnabled(bEnabled);
	m_ui.trackNavigateMenu->setEnabled(bTracks);
	m_ui.trackNavigateFirstAction->setEnabled(bTracks);
	m_ui.trackNavigatePrevAction->setEnabled(bEnabled && pTrack->prev() != NULL);
	m_ui.trackNavigateNextAction->setEnabled(bEnabled && pTrack->next() != NULL);
	m_ui.trackNavigateLastAction->setEnabled(bTracks);
	m_ui.trackNavigateNoneAction->setEnabled(bEnabled);
	m_ui.trackMoveMenu->setEnabled(bEnabled);
	m_ui.trackMoveTopAction->setEnabled(bEnabled && pTrack->prev() != NULL);
	m_ui.trackMoveUpAction->setEnabled(bEnabled && pTrack->prev() != NULL);
	m_ui.trackMoveDownAction->setEnabled(bEnabled && pTrack->next() != NULL);
	m_ui.trackMoveBottomAction->setEnabled(bEnabled && pTrack->next() != NULL);
	m_ui.trackCurveMenu->setEnabled(bEnabled);
	m_ui.trackImportAudioAction->setEnabled(m_pTracks != NULL);
	m_ui.trackImportMidiAction->setEnabled(m_pTracks != NULL);
	m_ui.trackAutoMonitorAction->setEnabled(m_pTracks != NULL);

	// Update track menu state...
	if (bEnabled) {
		m_ui.trackStateRecordAction->setChecked(pTrack->isRecord());
		m_ui.trackStateMuteAction->setChecked(pTrack->isMute());
		m_ui.trackStateSoloAction->setChecked(pTrack->isSolo());
		m_ui.trackStateMonitorAction->setChecked(pTrack->isMonitor());
	}

	// Update view menu state...
	m_ui.viewFilesAction->setChecked(
		m_pFiles && m_pFiles->isVisible());
	m_ui.viewMessagesAction->setChecked(
		m_pMessages && m_pMessages->isVisible());
	m_ui.viewConnectionsAction->setChecked(
		m_pConnections && m_pConnections->isVisible());
	m_ui.viewMixerAction->setChecked(
		m_pMixer && m_pMixer->isVisible());

	// Recent files menu.
	m_ui.fileOpenRecentMenu->setEnabled(m_pOptions->recentFiles.count() > 0);

	// Always make the latest message visible.
	if (m_pMessages)
		m_pMessages->flushStdoutBuffer();

	// Session status...
	updateTransportTime(iPlayHead);

	if (pTrack)
		m_statusItems[StatusName]->setText(pTrack->trackName().simplified());
	else
		m_statusItems[StatusName]->clear();

	if (m_iDirtyCount > 0)
		m_statusItems[StatusMod]->setText(tr("MOD"));
	else
		m_statusItems[StatusMod]->clear();

	if (m_pSession->recordTracks() > 0)
		m_statusItems[StatusRec]->setText(tr("REC"));
	else
		m_statusItems[StatusRec]->clear();

	if (m_pSession->muteTracks() > 0)
		m_statusItems[StatusMute]->setText(tr("MUTE"));
	else
		m_statusItems[StatusMute]->clear();

	if (m_pSession->soloTracks() > 0)
		m_statusItems[StatusSolo]->setText(tr("SOLO"));
	else
		m_statusItems[StatusSolo]->clear();

	if (m_pSession->isLooping())
		m_statusItems[StatusLoop]->setText(tr("LOOP"));
	else
		m_statusItems[StatusLoop]->clear();

	m_statusItems[StatusTime]->setText(
		m_pSession->timeScale()->textFromFrame(0, true, iSessionLength));

	m_statusItems[StatusRate]->setText(
		tr("%1 Hz").arg(m_pSession->sampleRate()));

	m_statusItems[StatusRec]->setPalette(*m_paletteItems[
		bRolling ? PaletteRed : PaletteNone]);
	m_statusItems[StatusMute]->setPalette(*m_paletteItems[
		m_pSession->muteTracks() > 0 ? PaletteYellow : PaletteNone]);
	m_statusItems[StatusSolo]->setPalette(*m_paletteItems[
		m_pSession->soloTracks() > 0 ? PaletteCyan : PaletteNone]);
	m_statusItems[StatusLoop]->setPalette(*m_paletteItems[
		bLooping ? PaletteGreen : PaletteNone]);

	// Transport stuff...
	m_ui.transportBackwardAction->setEnabled(bBumped);
	m_ui.transportRewindAction->setEnabled(bBumped);
	m_ui.transportFastForwardAction->setEnabled(!bRolling);
	m_ui.transportForwardAction->setEnabled(
		!bRolling && (iPlayHead < iSessionLength
			|| iPlayHead < m_pSession->editHead()
			|| iPlayHead < m_pSession->editTail()));
	m_ui.transportLoopAction->setEnabled(
		!bRolling && (bLooping || bSelectable));
	m_ui.transportLoopSetAction->setEnabled(
		!bRolling && bSelectable);
	m_ui.transportStopAction->setEnabled(bPlaying);
	m_ui.transportRecordAction->setEnabled(
		(!bLooping || !bPunching) && m_pSession->recordTracks() > 0);
	m_ui.transportPunchAction->setEnabled(bPunching || bSelectable);
	m_ui.transportPunchSetAction->setEnabled(bSelectable);
	m_ui.transportMetroAction->setEnabled(
		m_pOptions->bAudioMetronome || m_pOptions->bMidiMetronome);

	m_ui.transportRewindAction->setChecked(m_iTransportRolling < 0);
	m_ui.transportFastForwardAction->setChecked(m_iTransportRolling > 0);
	m_ui.transportLoopAction->setChecked(bLooping);
	m_ui.transportPlayAction->setChecked(bPlaying);
	m_ui.transportRecordAction->setChecked(bRecording);
	m_ui.transportPunchAction->setChecked(bPunching);

	// Special record mode settlement.
	m_pTimeSpinBox->setReadOnly(bRecording);
	m_pTempoSpinBox->setReadOnly(bRecording);

	m_pThumbView->update();
	m_pThumbView->updateThumb();

	// Update editors too...
	QListIterator<qtractorMidiEditorForm *> iter(m_editors);
	while (iter.hasNext())
		(iter.next())->stabilizeForm();
}


// Actually start all session engines.
bool qtractorMainForm::startSession (void)
{
	m_iTransportUpdate  = 0; 
	m_iTransportRolling = 0;
	m_bTransportPlaying = false;
	m_fTransportShuttle = 0.0f;
	m_iTransportStep    = 0;

	m_iXrunCount = 0;
	m_iXrunSkip  = 0;
	m_iXrunTimer = 0;

	m_iAudioRefreshTimer = 0;
	m_iMidiRefreshTimer  = 0;

	m_iPlayerTimer = 0;

	unsigned int iOldSampleRate = m_pSession->sampleRate();

	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
	bool bResult = m_pSession->open();
	QApplication::restoreOverrideCursor();

	if (bResult) {
		appendMessages(tr("Session started."));
		// HACK: Special treatment for disparate sample rates,
		// and only for (just loaded) non empty sessions...
		if (m_pSession->sampleRate() != iOldSampleRate
			&& m_pSession->sessionLength() > 0) {
			appendMessagesError(
				tr("The original session sample rate (%1 Hz)\n"
				"is not the same of the current audio engine (%2 Hz).\n\n"
				"Saving and reloading from a new session file\n"
				"is highly recommended.")
				.arg(iOldSampleRate)
				.arg(m_pSession->sampleRate()));
			// We'll doing the conversion right here and right now...
			QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
			m_pSession->updateSampleRate(iOldSampleRate);
			QApplication::restoreOverrideCursor();
			++m_iDirtyCount;
		}
	} else {
		// Uh-oh, we can't go on like this...
		appendMessagesError(
			tr("The audio/MIDI engine could not be started.\n\n"
			"Make sure the JACK audio server (jackd) and/or\n"
			"the ALSA Sequencer kernel module (snd-seq-midi)\n"
			"are up and running and then restart the session."));
	}

	return bResult;
}


// Check and restart session, if applicable.
bool qtractorMainForm::checkRestartSession (void)
{
	// Whether session is currently activated,
	// try to (re)open the whole thing...
	if (!m_pSession->isActivated()) {
		// Save current playhead position, if any...
		unsigned long iPlayHead = m_pSession->playHead();
		// Bail out if can't start it...
		if (!startSession()) {
			// Can go on with no-business...
			stabilizeForm();
			return false;
		}
		// Restore previous playhead position...
		m_pSession->setPlayHead(iPlayHead);
	}

	return true;
}


// Grab and restore current sampler channels session.
void qtractorMainForm::updateSession (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::updateSession()");
#endif

	// Initialize toolbar widgets...
//	m_pTempoSpinBox->setTempo(m_pSession->tempo(), false);
//	m_pTempoSpinBox->setBeatsPerBar(m_pSession->beatsPerBar(), false);
//	m_pTempoSpinBox->setBeatDivisor(m_pSession->beatDivisor(), false);
	m_pSnapPerBeatComboBox->setCurrentIndex(
		qtractorTimeScale::indexFromSnap(m_pSession->snapPerBeat()));

	// We're supposedly clean...
	m_iDirtyCount = 0;

	//  Actually (re)start session engines...
	if (startSession()) {
		// (Re)set playhead...
		if (m_ui.transportAutoBackwardAction->isChecked()) {
			if (m_iPlayHead > m_pSession->editHead())
				m_pSession->setPlayHead(m_pSession->editHead());
			else
				m_pSession->setPlayHead(0);
		}
		// (Re)initialize MIDI instrument patching...
		m_pSession->setMidiPatch(false); // Deferred++
		// Get on with the special ALSA sequencer notifier...
		if (m_pSession->midiEngine()->alsaNotifier()) {
			QObject::connect(m_pSession->midiEngine()->alsaNotifier(),
				SIGNAL(activated(int)),
				SLOT(alsaNotify()));			
		}
	}

	// Update the session views...
	viewRefresh();

	// We're definitely clean...
	qtractorSubject::resetQueue();

	// Ah, make it stand right.
	if (m_pTracks)
		m_pTracks->trackView()->setFocus();

	// Of course!...
	++m_iTransportUpdate;
}


// Update the track export menu.
void qtractorMainForm::updateExportMenu (void)
{
	// Special export enablement...
	int iAudioClips = 0;
	int iMidiClips = 0;
	
	if (!m_pSession->isPlaying()) {
		for (qtractorTrack *pTrack = m_pSession->tracks().first();
				pTrack; pTrack = pTrack->next()) {
			switch (pTrack->trackType()) {
			case qtractorTrack::Audio:
				iAudioClips += pTrack->clips().count();
				break;
			case qtractorTrack::Midi:
				iMidiClips += pTrack->clips().count();
				break;
			default:
				break;
			}
		}
	}

	m_ui.trackExportAudioAction->setEnabled(iAudioClips > 0);
	m_ui.trackExportMidiAction->setEnabled(iMidiClips > 0);
}


// Update the recent files list and menu.
void qtractorMainForm::updateRecentFiles ( const QString& sFilename )
{
	if (m_pOptions == NULL)
		return;

	// Remove from list if already there (avoid duplicates)
	int iIndex = m_pOptions->recentFiles.indexOf(sFilename);
	if (iIndex >= 0)
		m_pOptions->recentFiles.removeAt(iIndex);
	// Put it to front...
	m_pOptions->recentFiles.push_front(sFilename);
}


// Update the recent files list and menu.
void qtractorMainForm::updateRecentFilesMenu (void)
{
	if (m_pOptions == NULL)
		return;

	// Time to keep the list under limits.
	int iRecentFiles = m_pOptions->recentFiles.count();
	while (iRecentFiles > m_pOptions->iMaxRecentFiles) {
		m_pOptions->recentFiles.pop_back();
		--iRecentFiles;
	}

	// Rebuild the recent files menu...
	m_ui.fileOpenRecentMenu->clear();
	for (int i = 0; i < iRecentFiles; ++i) {
		const QString& sFilename = m_pOptions->recentFiles[i];
		if (QFileInfo(sFilename).exists()) {
			QAction *pAction = m_ui.fileOpenRecentMenu->addAction(
				QString("&%1 %2").arg(i + 1).arg(sessionName(sFilename)),
				this, SLOT(fileOpenRecent()));
			pAction->setData(i);
		}
	}

	// Settle as enabled?
	m_ui.fileOpenRecentMenu->setEnabled(!m_ui.fileOpenRecentMenu->isEmpty());
}


// Force update of the peak-files auto-remove mode.
void qtractorMainForm::updatePeakAutoRemove (void)
{
	if (m_pOptions == NULL)
		return;

	qtractorAudioPeakFactory *pAudioPeakFactory
		= m_pSession->audioPeakFactory();
	if (pAudioPeakFactory)
		pAudioPeakFactory->setAutoRemove(m_pOptions->bPeakAutoRemove);	
}


// Update main transport-time display format.
void qtractorMainForm::updateDisplayFormat (void)
{
	if (m_pOptions == NULL)
		return;

	// Main transport display format is due...
	qtractorTimeScale::DisplayFormat displayFormat;
	switch (m_pOptions->iDisplayFormat) {
	case 2:
		displayFormat = qtractorTimeScale::BBT;
		break;
	case 1:
		displayFormat = qtractorTimeScale::Time;
		break;
	case 0:
	default:
		displayFormat = qtractorTimeScale::Frames;
		break;
	}

	m_pSession->timeScale()->setDisplayFormat(displayFormat);
	m_pTimeSpinBox->updateDisplayFormat();
}


// Update plugins search paths (LV2_PATH).
void qtractorMainForm::updatePluginPaths (void)
{
	if (m_pOptions == NULL)
		return;

#ifdef CONFIG_LV2
	qtractorPluginPath path(qtractorPluginType::Lv2);
	path.setPaths(m_pOptions->lv2Paths);
	path.open();
#endif
}


// Update audio player parameters.
void qtractorMainForm::updateAudioPlayer (void)
{
	if (m_pOptions == NULL)
		return;

	// Configure the Audio engine player handling...
	qtractorAudioEngine *pAudioEngine = m_pSession->audioEngine();
	if (pAudioEngine == NULL)
		return;

	pAudioEngine->setPlayerBus(m_pOptions->bAudioPlayerBus);
}


// Update Audio engine control mode settings.
void qtractorMainForm::updateTransportMode (void)
{
	if (m_pOptions == NULL)
		return;

	// Configure the Audio engine handling...
	qtractorAudioEngine *pAudioEngine = m_pSession->audioEngine();
	if (pAudioEngine == NULL)
		return;

	pAudioEngine->setTransportMode(
		qtractorBus::BusMode(m_pOptions->iTransportMode));
}


// Update MIDI engine control mode settings.
void qtractorMainForm::updateMidiControlModes (void)
{
	if (m_pOptions == NULL)
		return;

	// Configure the MIDI engine handling...
	qtractorMidiEngine *pMidiEngine = m_pSession->midiEngine();
	if (pMidiEngine == NULL)
		return;

	pMidiEngine->setCaptureQuantize(
		qtractorTimeScale::snapFromIndex(m_pOptions->iMidiCaptureQuantize));
	pMidiEngine->setMmcDevice(m_pOptions->iMidiMmcDevice);
	pMidiEngine->setMmcMode(qtractorBus::BusMode(m_pOptions->iMidiMmcMode));
	pMidiEngine->setSppMode(qtractorBus::BusMode(m_pOptions->iMidiSppMode));
	pMidiEngine->setClockMode(qtractorBus::BusMode(m_pOptions->iMidiClockMode));
}


// Update MIDI playback queue timersetting.
void qtractorMainForm::updateMidiQueueTimer (void)
{
	if (m_pOptions == NULL)
		return;

	// Configure the MIDI engine player handling...
	qtractorMidiEngine *pMidiEngine = m_pSession->midiEngine();
	if (pMidiEngine == NULL)
		return;

	pMidiEngine->setAlsaTimer(m_pOptions->iMidiQueueTimer);
}


// Update MIDI control parameters.
void qtractorMainForm::updateMidiControl (void)
{
	if (m_pOptions == NULL)
		return;

	// Configure the MIDI engine player handling...
	qtractorMidiEngine *pMidiEngine = m_pSession->midiEngine();
	if (pMidiEngine == NULL)
		return;

	pMidiEngine->setControlBus(m_pOptions->bMidiControlBus);
}


// Update audio metronome parameters.
void qtractorMainForm::updateAudioMetronome (void)
{
	if (m_pOptions == NULL)
		return;

	// Configure the Audio engine metronome handling...
	qtractorAudioEngine *pAudioEngine = m_pSession->audioEngine();
	if (pAudioEngine == NULL)
		return;

	pAudioEngine->setMetroBarFilename(m_pOptions->sMetroBarFilename);
	pAudioEngine->setMetroBeatFilename(m_pOptions->sMetroBeatFilename);

	pAudioEngine->setMetroBarGain(m_pOptions->fMetroBarGain);
	pAudioEngine->setMetroBeatGain(m_pOptions->fMetroBeatGain);

	bool bAudioMetronome = m_pOptions->bAudioMetronome;
	pAudioEngine->setMetroBus(
		bAudioMetronome && m_pOptions->bAudioMetroBus);
	pAudioEngine->setMetroEnabled(bAudioMetronome);
	pAudioEngine->setMetronome(
		bAudioMetronome && m_ui.transportMetroAction->isChecked());
}


// Update MIDI metronome parameters.
void qtractorMainForm::updateMidiMetronome (void)
{
	if (m_pOptions == NULL)
		return;

	// Configure the MIDI engine metronome handling...
	qtractorMidiEngine *pMidiEngine = m_pSession->midiEngine();
	if (pMidiEngine == NULL)
		return;

	pMidiEngine->setMetroChannel(m_pOptions->iMetroChannel);
	pMidiEngine->setMetroBar(
		m_pOptions->iMetroBarNote,
		m_pOptions->iMetroBarVelocity,
		m_pOptions->iMetroBarDuration);
	pMidiEngine->setMetroBeat(
		m_pOptions->iMetroBeatNote,
		m_pOptions->iMetroBeatVelocity,
		m_pOptions->iMetroBeatDuration);

	bool bMidiMetronome = m_pOptions->bMidiMetronome;
	pMidiEngine->setMetroBus(
		bMidiMetronome && m_pOptions->bMidiMetroBus);
	pMidiEngine->setMetroEnabled(bMidiMetronome);
	pMidiEngine->setMetronome(
		bMidiMetronome && m_ui.transportMetroAction->isChecked());
}


// Curve/automation menu stabilizer.
void qtractorMainForm::updateCurveMenu (void)
{
	qtractorTrack *pTrack
		= (m_pTracks ? m_pTracks->currentTrack(): NULL);
	qtractorCurveList *pCurveList
		= (pTrack ? pTrack->curveList() : NULL);
	qtractorCurve *pCurrentCurve
		= (pCurveList ? pCurveList->currentCurve() : NULL);

	bool bEnabled = trackCurveSelectMenu(m_ui.trackCurveSelectMenu);
	m_ui.trackCurveSelectMenu->setEnabled(bEnabled);

	if (bEnabled)
		bEnabled = pCurveList->isEnabled();

	bool bCurveEnabled = bEnabled && pCurrentCurve && pCurrentCurve->isEnabled();
	if (bCurveEnabled)
		bCurveEnabled = trackCurveModeMenu(m_ui.trackCurveModeMenu);
	m_ui.trackCurveModeMenu->setEnabled(bCurveEnabled);

	m_ui.trackCurveProcessAction->setEnabled(bCurveEnabled);
	m_ui.trackCurveProcessAction->setChecked(
		pCurrentCurve && pCurrentCurve->isProcess());

	m_ui.trackCurveCaptureAction->setEnabled(bCurveEnabled);
	m_ui.trackCurveCaptureAction->setChecked(
		pCurrentCurve && pCurrentCurve->isCapture());

	m_ui.trackCurveClearAction->setEnabled(bCurveEnabled);

	m_ui.trackCurveProcessAllAction->setEnabled(bEnabled);
	m_ui.trackCurveProcessAllAction->setChecked(
		pCurveList && pCurveList->isProcessAll());
	m_ui.trackCurveProcessAllAction->setEnabled(bEnabled);

	m_ui.trackCurveCaptureAllAction->setEnabled(bEnabled);
	m_ui.trackCurveCaptureAllAction->setChecked(
		pCurveList && pCurveList->isCaptureAll());
	m_ui.trackCurveCaptureAllAction->setEnabled(bEnabled);

	m_ui.trackCurveClearAllAction->setEnabled(
		pCurveList && pCurveList->count() > 0);
}


// Track curve/automation select menu builder.
bool qtractorMainForm::trackCurveSelectMenu ( QMenu *pMenu ) const
{
	pMenu->clear();

	if (m_pTracks == NULL)
		return false;

	qtractorTrack *pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return false;

	qtractorCurveList *pCurveList = pTrack->curveList();
	if (pCurveList == NULL)
		return false;

	qtractorMainForm *pMainForm = qtractorMainForm::getInstance();
	if (pMainForm == NULL)
		return false;

	qtractorMixer *pMixer = pMainForm->mixer();
	if (pMixer == NULL)
		return false;

	qtractorMixerStrip *pMixerStrip
		= pMixer->trackRack()->findStrip(pTrack->monitor());
	if (pMixerStrip == NULL)
		return false;

	qtractorCurve *pCurrentCurve
		= pCurveList->currentCurve();
	qtractorSubject *pCurrentSubject
		= (pCurrentCurve ? pCurrentCurve->subject() : NULL);

	QAction *pAction;
	qtractorSubject *pSubject;

	pSubject = pTrack->monitorSubject();
	pAction = pMenu->addAction(pSubject->name());
	pAction->setCheckable(true);
	pAction->setChecked(pCurrentSubject == pSubject);
	pAction->setData(qVariantFromValue(pSubject));

	pSubject = pMixerStrip->meter()->panningSubject();
	pAction = pMenu->addAction(pSubject->name());
	pAction->setCheckable(true);
	pAction->setChecked(pCurrentSubject == pSubject);
	pAction->setData(qVariantFromValue(pSubject));

	pSubject = pMixerStrip->meter()->gainSubject();
	pAction = pMenu->addAction(pSubject->name());
	pAction->setCheckable(true);
	pAction->setChecked(pCurrentSubject == pSubject);
	pAction->setData(qVariantFromValue(pSubject));

	pMenu->addSeparator();

	pSubject = pTrack->recordSubject();
	pAction = pMenu->addAction(pSubject->name());
	pAction->setCheckable(true);
	pAction->setChecked(pCurrentSubject == pSubject);
	pAction->setData(qVariantFromValue(pSubject));

	pSubject = pTrack->muteSubject();
	pAction = pMenu->addAction(pSubject->name());
	pAction->setCheckable(true);
	pAction->setChecked(pCurrentSubject == pSubject);
	pAction->setData(qVariantFromValue(pSubject));

	pSubject = pTrack->soloSubject();
	pAction = pMenu->addAction(pSubject->name());
	pAction->setCheckable(true);
	pAction->setChecked(pCurrentSubject == pSubject);
	pAction->setData(qVariantFromValue(pSubject));

	qtractorPluginList *pPluginList = pTrack->pluginList();
	if (pPluginList->count() > 0) {
		pMenu->addSeparator();
		qtractorPlugin *pPlugin = pPluginList->first();
		while (pPlugin) {
			const qtractorPlugin::Params& params = pPlugin->params();
			if (params.count() > 0) {
				QMenu *pParamMenu = pMenu->addMenu(pPlugin->type()->name());
				qtractorPlugin::Params::ConstIterator param
					= params.constBegin();
				for ( ; param != params.constEnd(); ++param) {
					pSubject = param.value()->subject();
					pAction = pParamMenu->addAction(pSubject->name());
					pAction->setCheckable(true);
					pAction->setChecked(pCurrentSubject == pSubject);
					pAction->setData(qVariantFromValue(pSubject));
				}
			}
			pPlugin = pPlugin->next();
		}
	}

	pMenu->addSeparator();

	pSubject = NULL;
	pAction = pMenu->addAction(tr("&None"));
	pAction->setCheckable(true);
	pAction->setChecked(pCurrentSubject == pSubject);
	pAction->setData(qVariantFromValue(pSubject));
	
	return true;
}


bool qtractorMainForm::trackCurveModeMenu ( QMenu *pMenu ) const
{
	pMenu->clear();

	qtractorTrack *pTrack = m_pTracks->currentTrack();
	if (pTrack == NULL)
		return false;

	qtractorCurve *pCurrentCurve = pTrack->currentCurve();
	if (pCurrentCurve == NULL)
		return false;

	qtractorCurve::Mode mode = pCurrentCurve->mode();
	bool bToggled = (pCurrentCurve->subject())->isToggled();

	QAction *pAction;

	pAction = pMenu->addAction(tr("&Hold"));
	pAction->setCheckable(true);
	pAction->setChecked(mode == qtractorCurve::Hold);
	pAction->setData(int(qtractorCurve::Hold));

	pAction = pMenu->addAction(tr("&Linear"));
	pAction->setCheckable(true);
	pAction->setChecked(mode == qtractorCurve::Linear);
	pAction->setData(int(qtractorCurve::Linear));
	pAction->setEnabled(!bToggled);

	pAction = pMenu->addAction(tr("&Spline"));
	pAction->setCheckable(true);
	pAction->setChecked(mode == qtractorCurve::Spline);
	pAction->setData(int(qtractorCurve::Spline));
	pAction->setEnabled(!bToggled);

	return true;
}


// Clip menu stabilizer.
void qtractorMainForm::updateClipMenu (void)
{
	unsigned long iPlayHead = m_pSession->playHead();

	qtractorTrack *pTrack = NULL;
	qtractorClip  *pClip  = NULL;
	bool bTracks = (m_pTracks && m_pSession->tracks().count() > 0);
	if (bTracks) {
		pTrack = m_pTracks->currentTrack();
		pClip  = m_pTracks->currentClip();
	}

	bool bEnabled    = (pTrack != NULL);
	bool bSelected   = (m_pTracks && m_pTracks->isClipSelected());
	bool bSelectable = (m_pSession->editHead() < m_pSession->editTail());

	bool bSingleTrackSelected = ((pClip != NULL || bSelected)
		&& (pTrack == NULL || m_pTracks->singleTrackSelected() == pTrack));

	m_ui.editClipNewAction->setEnabled(bEnabled);
	m_ui.editClipEditAction->setEnabled(pClip != NULL);
	m_ui.editClipSplitAction->setEnabled(pClip != NULL
		&& iPlayHead > pClip->clipStart()
		&& iPlayHead < pClip->clipStart() + pClip->clipLength());
	m_ui.editClipMergeAction->setEnabled(bSingleTrackSelected);
	m_ui.editClipNormalizeAction->setEnabled(pClip != NULL || bSelected);
	m_ui.editClipToolsMenu->setEnabled((pClip != NULL || bSelected)
		&& pTrack && pTrack->trackType() == qtractorTrack::Midi);
	m_ui.editClipTempoAction->setEnabled(pClip != NULL || bSelectable);
	m_ui.editClipImportAction->setEnabled(bTracks);
		// pTrack && pTrack->trackType() == qtractorTrack::Audio);
	m_ui.editClipExportAction->setEnabled(bSingleTrackSelected);
}


// Zoom view menu stabilizer.
void qtractorMainForm::updateZoomMenu (void)
{
	int iZoomMode = qtractorTracks::ZoomNone;
	if (m_pTracks)
		iZoomMode = m_pTracks->zoomMode();

	m_ui.viewZoomHorizontalAction->setChecked(
		iZoomMode == qtractorTracks::ZoomHorizontal);
	m_ui.viewZoomVerticalAction->setChecked(
		iZoomMode == qtractorTracks::ZoomVertical);
	m_ui.viewZoomAllAction->setChecked(
		iZoomMode == qtractorTracks::ZoomAll);
}


// Snap-per-beat view menu builder.
void qtractorMainForm::updateSnapMenu (void)
{
	m_ui.viewSnapMenu->clear();

	int iSnapCurrent
		= qtractorTimeScale::indexFromSnap(m_pSession->snapPerBeat());

	int iSnap = 0;
	QStringListIterator iter(qtractorTimeScale::snapItems());
	while (iter.hasNext()) {
		QAction *pAction = m_ui.viewSnapMenu->addAction(
			iter.next(), this, SLOT(viewSnap()));
		pAction->setCheckable(true);
		pAction->setChecked(iSnap == iSnapCurrent);
		pAction->setData(iSnap++);
	}

	m_ui.viewSnapMenu->addSeparator();
	m_ui.viewSnapMenu->addAction(m_ui.viewSnapGridAction);
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Messages window form handlers.

// Messages output methods.
void qtractorMainForm::appendMessages( const QString& s )
{
	if (m_pMessages)
		m_pMessages->appendMessages(s);

	statusBar()->showMessage(s, 3000);
}

void qtractorMainForm::appendMessagesColor( const QString& s, const QString& c )
{
	if (m_pMessages)
		m_pMessages->appendMessagesColor(s, c);

	statusBar()->showMessage(s, 3000);
}

void qtractorMainForm::appendMessagesText( const QString& s )
{
	if (m_pMessages)
		m_pMessages->appendMessagesText(s);
}

void qtractorMainForm::appendMessagesError( const QString& s )
{
	if (m_pMessages)
		m_pMessages->show();

	appendMessagesColor(s.simplified(), "#ff0000");

	QMessageBox::critical(this, tr("Error") + " - " QTRACTOR_TITLE, s);
}


// Force update of the messages font.
void qtractorMainForm::updateMessagesFont (void)
{
	if (m_pOptions == NULL)
		return;

	if (m_pMessages && !m_pOptions->sMessagesFont.isEmpty()) {
		QFont font;
		if (font.fromString(m_pOptions->sMessagesFont))
			m_pMessages->setMessagesFont(font);
	}
}


// Update messages window line limit.
void qtractorMainForm::updateMessagesLimit (void)
{
	if (m_pOptions == NULL)
		return;

	if (m_pMessages) {
		if (m_pOptions->bMessagesLimit)
			m_pMessages->setMessagesLimit(m_pOptions->iMessagesLimitLines);
		else
			m_pMessages->setMessagesLimit(-1);
	}
}


// Enablement of the messages capture feature.
void qtractorMainForm::updateMessagesCapture (void)
{
	if (m_pOptions == NULL)
		return;

	if (m_pMessages)
		m_pMessages->setCaptureEnabled(m_pOptions->bStdoutCapture);
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Editors stuff.

void qtractorMainForm::addEditorForm ( qtractorMidiEditorForm *pEditorForm )
{
	if (m_editors.indexOf(pEditorForm) < 0)
		m_editors.append(pEditorForm);
}

void qtractorMainForm::removeEditorForm ( qtractorMidiEditorForm *pEditorForm )
{
	int iEditorForm = m_editors.indexOf(pEditorForm);
	if (iEditorForm >= 0)
		m_editors.removeAt(iEditorForm);
}


//-------------------------------------------------------------------------
// qtractorMainForm -- Timer stuff.

// Timer slot funtion.
void qtractorMainForm::timerSlot (void)
{
	// Avoid stabilize re-entrancy...
	if (m_pSession->isBusy()) {
		// Register the next timer slot.
		QTimer::singleShot(QTRACTOR_TIMER_DELAY, this, SLOT(timerSlot()));
		return;
	}
		
	// Currrent state...
	bool bPlaying  = m_pSession->isPlaying();
	long iPlayHead = long(m_pSession->playHead());

	qtractorAudioEngine *pAudioEngine = m_pSession->audioEngine();
	qtractorMidiEngine  *pMidiEngine  = m_pSession->midiEngine();

	// Playhead status...
	if (iPlayHead != long(m_iPlayHead)) {
		m_iPlayHead = iPlayHead;
		if (m_pTracks) {
			// Update tracks-view play-head...
			m_pTracks->trackView()->setPlayHead(iPlayHead,
				m_ui.transportFollowAction->isChecked());
			// Update editors play-head...
			QListIterator<qtractorMidiEditorForm *> iter(m_editors);
			while (iter.hasNext())
				(iter.next()->editor())->setPlayHead(iPlayHead);
		}
		if (!bPlaying && m_iTransportRolling == 0 && m_iTransportStep == 0) {
			// Update transport status anyway...
			++m_iTransportUpdate;
			// Send MMC LOCATE command...
			if (!pAudioEngine->isFreewheel()) {
				pMidiEngine->sendMmcLocate(
					m_pSession->locateFromFrame(iPlayHead));
				pMidiEngine->sendSppCommand(SND_SEQ_EVENT_SONGPOS,
					m_pSession->songPosFromFrame(iPlayHead));
			}
		}
	}

	// Transport status...
	if (m_iTransportUpdate > 0) {
		// Do some transport related tricks...
		if (m_iTransportRolling == 0) {
			m_iTransportUpdate = 0;
			if (m_iTransportStep) {
				// Transport stepping over...
				iPlayHead += (m_iTransportStep
					* long(m_pSession->frameFromTick(m_pSession->ticksPerBeat())));
				if (iPlayHead < 0)
					iPlayHead = 0;
				m_iTransportStep = 0;
				// Make it thru...
				m_pSession->setPlayHead(m_pSession->frameSnap(iPlayHead));
			}
		} else {
			// Transport rolling over...
			iPlayHead += long(m_fTransportShuttle
				* float(m_pSession->sampleRate())) >> 1;
			if (iPlayHead < 0) {
				iPlayHead = 0;
				m_iTransportUpdate = 0;
				// Stop playback for sure...
				if (setPlaying(false)) {
					// Send MMC STOP command...
					pMidiEngine->sendMmcCommand(qtractorMmcEvent::STOP);
					pMidiEngine->sendSppCommand(SND_SEQ_EVENT_STOP);
				}
			}
			// Make it thru...
			m_pSession->setPlayHead(iPlayHead);
		}
		// Ensure track-view into visibility...
		if (m_pTracks && m_ui.transportFollowAction->isChecked())
			m_pTracks->trackView()->ensureVisibleFrame(iPlayHead);
		// Take the change to give some visual feedback...
		if (m_iTransportUpdate > 0) {
			updateTransportTime(iPlayHead);
			m_pThumbView->updateThumb();
		} else {
			stabilizeForm();
		}
		// Done with transport tricks.
	} else {
		// Read JACK transport state and react if out-of-sync..
		jack_client_t *pJackClient = NULL;
		if (pAudioEngine->transportMode() & qtractorBus::Input)
			pJackClient = pAudioEngine->jackClient();
		if (pJackClient && !pAudioEngine->isFreewheel()) {
			jack_position_t pos;
			jack_transport_state_t state
				= jack_transport_query(pJackClient, &pos);
			// 1. Check on external transport state request changes...
			if ((state == JackTransportStopped &&  bPlaying) ||
				(state == JackTransportRolling && !bPlaying)) {
			#ifdef CONFIG_DEBUG
				qDebug("qtractorMainForm::timerSlot()"
					" playing=%d state=%d",
					int(bPlaying), int(state == JackTransportRolling));
			#endif
				iPlayHead = pos.frame;
				if (!bPlaying)
					m_pSession->seek(iPlayHead, true);
				transportPlay(); // Toggle playing!
			//	if (bPlaying)
			//		m_pSession->seek(iPlayHead, true);
			}
		}
		// Check if its time to refresh playhead timer...
		if (bPlaying && m_iPlayTimer < QTRACTOR_TIMER_DELAY) {
			m_iPlayTimer += QTRACTOR_TIMER_MSECS;
			if (m_iPlayTimer >= QTRACTOR_TIMER_DELAY) {
				m_iPlayTimer = 0;
				updateTransportTime(iPlayHead);
				// If recording update track view and session length, anyway...
				if (m_pTracks && m_pSession->isRecording()) {
					// HACK: Care of punch-out...
					if (m_pSession->isPunching()
						&& iPlayHead > long(m_pSession->punchOut())) {
						if (setRecording(false)) {
							// Send MMC RECORD_EXIT command...
							pMidiEngine->sendMmcCommand(
								qtractorMmcEvent::RECORD_EXIT);
							++m_iTransportUpdate;
						}
					} else {
						m_pTracks->trackView()->updateContentsRecord();
						m_pSession->updateSessionLength(iPlayHead);
						m_statusItems[StatusTime]->setText(
							m_pSession->timeScale()->textFromFrame(
								0, true, m_pSession->sessionLength()));
					}
				}
				else
				// Whether to continue past end...
				if (!m_ui.transportContinueAction->isChecked()
					&& m_iPlayHead > m_pSession->sessionLength()
					&& m_iPlayHead > m_pSession->loopEnd()) {
					if (m_pSession->isLooping()) {
						// Maybe it's better go on with looping, eh?
						m_pSession->setPlayHead(m_pSession->loopStart());
						++m_iTransportUpdate;
					}
					else
					// Auto-backward reset feature...
					if (m_ui.transportAutoBackwardAction->isChecked()) {
						if (m_iPlayHead > m_pSession->editHead())
							m_pSession->setPlayHead(m_pSession->editHead());
						else
							m_pSession->setPlayHead(0);
						++m_iTransportUpdate;
					} else {
						// Stop at once!
						transportPlay();
					}
				}
			}
		}
	}

	// Check if its time to refresh some tracks...
	if (m_iPeakTimer > 0) {
		m_iPeakTimer -= QTRACTOR_TIMER_MSECS;
		if (m_iPeakTimer < QTRACTOR_TIMER_MSECS) {
			m_iPeakTimer = 0;
			if (m_pTracks && m_pTracks->trackView())
				m_pTracks->trackView()->updateContents();
		}
	}

	// Check if we've got some XRUN callbacks...
	if (m_iXrunTimer > 0) {
		m_iXrunTimer -= QTRACTOR_TIMER_MSECS;
		if (m_iXrunTimer < QTRACTOR_TIMER_MSECS) {
			m_iXrunTimer = 0;
			// Did we skip any?
			if (m_iXrunSkip > 0) {
				appendMessagesColor(
					tr("XRUN(%1 skipped)").arg(m_iXrunSkip), "#cc99cc");
				m_iXrunSkip = 0;
			}
			// Just post an informative message...
			appendMessagesColor(
				tr("XRUN(%1): some frames might have been lost.")
				.arg(m_iXrunCount), "#cc0033");
		}
	}

	// Check if its time to refresh Audio connections...
	if (m_iAudioRefreshTimer > 0) {
		m_iAudioRefreshTimer -= QTRACTOR_TIMER_MSECS;
		if (m_iAudioRefreshTimer < QTRACTOR_TIMER_MSECS) {
			m_iAudioRefreshTimer = 0;
			if (pAudioEngine->updateConnects() == 0) {
				appendMessagesColor(
					tr("Audio connections change."), "#cc9966");
				if (m_pConnections)
					m_pConnections->connectForm()->audioRefresh();
			}
		}
	}

	// MIDI connections should be checked too...
	if (m_iMidiRefreshTimer > 0) {
		m_iMidiRefreshTimer -= QTRACTOR_TIMER_MSECS;
		if (m_iMidiRefreshTimer < QTRACTOR_TIMER_MSECS) {
			m_iMidiRefreshTimer = 0;
			if (pMidiEngine->updateConnects() == 0) {
				appendMessagesColor(
					tr("MIDI connections change."), "#66cc99");
				if (m_pConnections)
					m_pConnections->connectForm()->midiRefresh();
			}
		}
	}

	// Check if its time to refresh audition/pre-listening status...
	if (m_iPlayerTimer > 0) {
		m_iPlayerTimer -= QTRACTOR_TIMER_MSECS;
		if (m_iPlayerTimer < QTRACTOR_TIMER_MSECS) {
			m_iPlayerTimer = 0;
			if (m_pFiles) {
				if (m_pFiles->isPlayState()) {
					if (pAudioEngine->isPlayerOpen()) {
						m_iPlayerTimer += QTRACTOR_TIMER_DELAY << 2;
					} else {
						appendMessages(tr("Playing ended."));
						m_pFiles->setPlayState(false);
					}
				}
				else if (pAudioEngine->isPlayerOpen()) {
					m_iPlayerTimer += QTRACTOR_TIMER_DELAY << 2;
					m_pFiles->setPlayState(true);
				}
			}
		}
	}

	// Always update mixer monitoring...
	if (m_pMixer)
		m_pMixer->refresh();

	// Asynchronous observer update...
	qtractorSubject::flushQueue();

#ifdef CONFIG_DSSI
#ifdef CONFIG_LIBLO
	if ((m_iIdleTimer += QTRACTOR_TIMER_MSECS) >= QTRACTOR_TIMER_DELAY) {
		m_iIdleTimer = 0;
		qtractorDssiPlugin::idleEditorAll();
	}
#endif
#endif
#ifdef CONFIG_LV2_EXTERNAL_UI
	qtractorLv2Plugin::idleEditorAll();
#endif

	// Register the next timer slot.
	QTimer::singleShot(QTRACTOR_TIMER_MSECS, this, SLOT(timerSlot()));
}


//-------------------------------------------------------------------------
// qtractorMainForm -- MIDI engine notifications.

// Audio file peak notification slot.
void qtractorMainForm::peakNotify (void)
{
	// A peak file has just been (re)created;
	// try to postpone the event effect a little more...
	if (m_iPeakTimer  < QTRACTOR_TIMER_DELAY)
		m_iPeakTimer += QTRACTOR_TIMER_DELAY;
}


// ALSA sequencer notification slot.
void qtractorMainForm::alsaNotify (void)
{
	// This specialty needs acknowledgement...
	m_pSession->midiEngine()->alsaNotifyAck();

	// A MIDI graph change has just been occurred;
	// try to postpone the event effect a little more...
	if (m_iMidiRefreshTimer  < QTRACTOR_TIMER_DELAY)
		m_iMidiRefreshTimer += QTRACTOR_TIMER_DELAY;
}


// Custom audio shutdown event handler.
void qtractorMainForm::audioShutNotify (void)
{
	// Engine shutdown is on demand...
	m_pSession->shutdown();
	m_pConnections->clear();

	// Send an informative message box...
	appendMessagesError(
		tr("The audio engine has been shutdown.\n\n"
		"Make sure the JACK audio server (jackd)\n"
		"is up and running and then restart session."));

	// Make things just bearable...
	stabilizeForm();
}


// Custom audio XRUN event handler.
void qtractorMainForm::audioXrunNotify (void)
{
	// An XRUN has just been notified...
	++m_iXrunCount;

	// Skip this one, maybe we're under some kind of storm;
	if (m_iXrunTimer > 0)
		++m_iXrunSkip;

	// Defer the informative effect...
	if (m_iXrunTimer  < QTRACTOR_TIMER_DELAY)
		m_iXrunTimer += QTRACTOR_TIMER_DELAY;
}


// Custom audio port/graph change event handler.
void qtractorMainForm::audioPortNotify (void)
{
	// An Audio graph change has just been issued;
	// try to postpone the event effect a little more...
	if (m_iAudioRefreshTimer  < QTRACTOR_TIMER_DELAY)
		m_iAudioRefreshTimer += QTRACTOR_TIMER_DELAY;
}


// Custom audio buffer size change event handler.
void qtractorMainForm::audioBuffNotify (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::audioBuffNotify()");
#endif

	audioShutNotify();
}


// Custom (JACK) session event handler.
void qtractorMainForm::audioSessNotify ( void *pvSessionArg )
{
#ifdef CONFIG_JACK_SESSION

	qtractorAudioEngine *pAudioEngine = m_pSession->audioEngine();
	if (pAudioEngine == NULL)
		return;

	jack_client_t *pJackClient = pAudioEngine->jackClient();
	if (pJackClient == NULL)
		return;

	jack_session_event_t *pJackSessionEvent
		= (jack_session_event_t *) pvSessionArg;
	
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::audioSessNotify()"
		" type=%d client_uuid=\"%s\" session_dir=\"%s\"",
		int(pJackSessionEvent->type),
		pJackSessionEvent->client_uuid,
		pJackSessionEvent->session_dir);
#endif

	bool bTemplate = (pJackSessionEvent->type == JackSessionSaveTemplate);
	bool bQuit = (pJackSessionEvent->type == JackSessionSaveAndQuit);

	if (m_pSession->sessionName().isEmpty())
		editSession();

	QString sSessionName = m_pSession->sessionName();
	if (sSessionName.isEmpty())
		sSessionName = tr("Untitled%1").arg(m_iUntitled);
	
	const QString sSessionDir
		= QString::fromUtf8(pJackSessionEvent->session_dir);
	const QString sSessionFile = sSessionName + '.'
		+ (bTemplate
			? qtractorDocument::templateExt()
			: qtractorDocument::archiveExt());

	QStringList args;
	args << QApplication::applicationFilePath();
	args << QString("--session-id=%1").arg(pJackSessionEvent->client_uuid);

	const QString sFilename
		= QFileInfo(sSessionDir, sSessionFile).absoluteFilePath();

	if (saveSessionFileEx(sFilename, bTemplate))
		args << QString("\"${SESSION_DIR}%1\"").arg(sSessionFile);

	const QByteArray aCmdLine = args.join(" ").toUtf8();
	pJackSessionEvent->command_line = strdup(aCmdLine.constData());

	jack_session_reply(pJackClient, pJackSessionEvent);
	jack_session_event_free(pJackSessionEvent);

	if (bQuit)
		close();

#endif
}


// Custom (JACK) transport sync event handler.
void qtractorMainForm::audioSyncNotify ( unsigned long iPlayHead )
{
	if (m_pSession->isBusy())
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::audioSyncNotify(%lu)", iPlayHead);
#endif

	m_pSession->setPlayHead(iPlayHead);
	++m_iTransportUpdate;
}




// Custom MMC event handler.
void qtractorMainForm::midiMmcNotify ( const qtractorMmcEvent& mmce )
{
	QString sMmcText("MIDI MMC: ");
	switch (mmce.cmd()) {
	case qtractorMmcEvent::STOP:
	case qtractorMmcEvent::PAUSE:
		sMmcText += tr("STOP");
		setPlaying(false);
		break;
	case qtractorMmcEvent::PLAY:
	case qtractorMmcEvent::DEFERRED_PLAY:
		sMmcText += tr("PLAY");
		setPlaying(true);
		break;
	case qtractorMmcEvent::FAST_FORWARD:
		sMmcText += tr("FFWD");
		setRolling(+1);
		break;
	case qtractorMmcEvent::REWIND:
		sMmcText += tr("REW");
		setRolling(-1);
		break;
	case qtractorMmcEvent::RECORD_STROBE:
	case qtractorMmcEvent::RECORD_PAUSE:
		sMmcText += tr("REC ON");
		if (!setRecording(true)) {
			// Send MMC RECORD_EXIT command immediate reply...
			m_pSession->midiEngine()->sendMmcCommand(
				qtractorMmcEvent::RECORD_EXIT);
		}
		break;
	case qtractorMmcEvent::RECORD_EXIT:
		sMmcText += tr("REC OFF");
		setRecording(false);
		break;
	case qtractorMmcEvent::MMC_RESET:
		sMmcText += tr("RESET");
		setRolling(0);
		break;
	case qtractorMmcEvent::LOCATE:
		sMmcText += tr("LOCATE %1").arg(mmce.locate());
		setLocate(mmce.locate());
		break;
	case qtractorMmcEvent::SHUTTLE:
		sMmcText += tr("SHUTTLE %1").arg(mmce.shuttle());
		setShuttle(mmce.shuttle());
		break;
	case qtractorMmcEvent::STEP:
		sMmcText += tr("STEP %1").arg(mmce.step());
		setStep(mmce.step());
		break;
	case qtractorMmcEvent::MASKED_WRITE:
		switch (mmce.scmd()) {
		case qtractorMmcEvent::TRACK_RECORD:
			sMmcText += tr("TRACK RECORD %1 %2")
				.arg(mmce.track())
				.arg(mmce.isOn());
			break;
		case qtractorMmcEvent::TRACK_MUTE:
			sMmcText += tr("TRACK MUTE %1 %2")
				.arg(mmce.track())
				.arg(mmce.isOn());
			break;
		case qtractorMmcEvent::TRACK_SOLO:
			sMmcText += tr("TRACK SOLO %1 %2")
				.arg(mmce.track())
				.arg(mmce.isOn());
			break;
		default:
			sMmcText += tr("Unknown sub-command");
			break;
		}
		setTrack(mmce.scmd(), mmce.track(), mmce.isOn());
		break;
	default:
		sMmcText += tr("Not implemented");
		break;
	}

	appendMessages(sMmcText);
	stabilizeForm();
}


// Custom controller event handler.
void qtractorMainForm::midiCtlNotify ( const qtractorCtlEvent& ctle )
{
	QString sCtlText(tr("MIDI CTL: %1, Channel %2, Param %3, Value %4")
		.arg(qtractorMidiControl::nameFromType(ctle.type()))
		.arg(ctle.channel() + 1)
		.arg(ctle.param())
		.arg(ctle.value()));

#ifdef CONFIG_DEBUG
	qDebug(sCtlText.toUtf8().constData());
#endif

	// Check if controller is used as MIDI controller...
	if (m_pMidiControl->processEvent(ctle)) {
		appendMessages(sCtlText);
		return;
	}

	if (ctle.type() == qtractorMidiEvent::CONTROLLER) {
		/* FIXME: JLCooper faders (as from US-224)...
		if (ctle.channel() == 15) {
			// Event translation...
			int   iTrack = int(ctle.controller()) & 0x3f;
			float fGain  = float(ctle.value()) / 127.0f;
			// Find the track by number...
			qtractorTrack *pTrack = m_pSession->tracks().at(iTrack);
			if (pTrack) {
				m_pSession->execute(
					new qtractorTrackGainCommand(pTrack, fGain, true));
				sCtlText += ' ';
				sCtlText += tr("(track %1, gain %2)")
					.arg(iTrack).arg(fGain);
				appendMessages(sCtlText);
			}
		}
		else */
		// Handle volume controls...
		if (ctle.param() == 7) {
			int iTrack = 0;
			float fGain = float(ctle.value()) / 127.0f;
			for (qtractorTrack *pTrack = m_pSession->tracks().first();
					pTrack; pTrack = pTrack->next()) {
				if (pTrack->trackType() == qtractorTrack::Midi &&
					pTrack->midiChannel() == ctle.channel()) {
					m_pSession->execute(
						new qtractorTrackGainCommand(pTrack, fGain, true));
					sCtlText += ' ';
					sCtlText += tr("(track %1, gain %2)")
						.arg(iTrack).arg(fGain);
					appendMessages(sCtlText);
				}
				++iTrack;
			}
		}
		else
		// Handle pan controls...
		if (ctle.param() == 10) {
			int iTrack = 0;
			float fPanning = (float(ctle.value()) - 64.0f) / 63.0f;
			for (qtractorTrack *pTrack = m_pSession->tracks().first();
					pTrack; pTrack = pTrack->next()) {
				if (pTrack->trackType() == qtractorTrack::Midi &&
					pTrack->midiChannel() == ctle.channel()) {
					m_pSession->execute(
						new qtractorTrackPanningCommand(pTrack, fPanning, true));
					sCtlText += ' ';
					sCtlText += tr("(track %1, panning %2)")
						.arg(iTrack).arg(fPanning);
					appendMessages(sCtlText);
				}
				++iTrack;
			}
		}
	}
}


// Custom MIDI SPP  event handler.
void qtractorMainForm::midiSppNotify ( int iSppCmd, unsigned short iSongPos )
{
	QString sSppText("MIDI SPP: ");
	switch (iSppCmd) {
	case SND_SEQ_EVENT_START:
		sSppText += tr("START");
		setSongPos(0);
		setPlaying(true);
		break;
	case SND_SEQ_EVENT_STOP:
		sSppText += tr("STOP");
		setPlaying(false);
		break;
	case SND_SEQ_EVENT_CONTINUE:
		sSppText += tr("CONTINUE");
		setPlaying(true);
		break;
	case SND_SEQ_EVENT_SONGPOS:
		sSppText += tr("SONGPOS %1").arg(iSongPos);
		setSongPos(iSongPos);
		break;
	default:
		sSppText += tr("Not implemented");
		break;
	}

	appendMessages(sSppText);
	stabilizeForm();
}


// Custom MIDI Clock event handler.
void qtractorMainForm::midiClkNotify ( float fTempo )
{
	QString sClkText("MIDI CLK: ");
	sClkText += tr("%1 BPM").arg(fTempo);
	appendMessages(sClkText);

	// Find appropriate node...
	qtractorTimeScale *pTimeScale = m_pSession->timeScale();
	qtractorTimeScale::Cursor& cursor = pTimeScale->cursor();
	qtractorTimeScale::Node *pNode = cursor.seekFrame(m_pSession->playHead());

	// Now, express the change immediately...
	if (pNode->prev()) {
		pNode->tempo = fTempo;
		pTimeScale->updateNode(pNode);
	} else {
		m_pSession->setTempo(fTempo);
	}
	++m_iTransportUpdate;

	updateContents(NULL, true);
	stabilizeForm();
}


//-------------------------------------------------------------------------
// qtractorMainForm -- General contents change stuff.

// Audio file addition slot funtion.
void qtractorMainForm::addAudioFile ( const QString& sFilename )
{
	// Add the just dropped audio file...
	if (m_pFiles)
		m_pFiles->addAudioFile(sFilename);

	stabilizeForm();
}


// Audio file selection slot funtion.
void qtractorMainForm::selectAudioFile (
	const QString& sFilename, int iTrackChannel, bool bSelect )
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::selectAudioFile(\"%s\", %d, %d)",
		sFilename.toUtf8().constData(), iTrackChannel, int(bSelect));
#endif

	// Select audio file...
	if (m_pTracks) {
		m_pTracks->trackView()->selectFile(
			qtractorTrack::Audio, sFilename, iTrackChannel, bSelect);
	}

	stabilizeForm();
}


// Audio file activation slot funtion.
void qtractorMainForm::activateAudioFile (
	const QString& sFilename, int /*iTrackChannel*/ )
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::selectAudioFile(\"%s\")",
		sFilename.toUtf8().constData());
#endif

	// Make sure session is activated...
	checkRestartSession();

	// We'll start playing if the file is valid, otherwise
	// the player is stopped (eg. empty filename)...
	qtractorAudioEngine *pAudioEngine = m_pSession->audioEngine();
	if (pAudioEngine && pAudioEngine->openPlayer(sFilename)) {
		appendMessages(tr("Playing \"%1\"...")
			.arg(QFileInfo(sFilename).fileName()));
	}

	// Try updating player status anyway...
	if (m_iPlayerTimer  < QTRACTOR_TIMER_DELAY)
		m_iPlayerTimer += QTRACTOR_TIMER_DELAY;

	stabilizeForm();
}


// MIDI file addition slot funtion.
void qtractorMainForm::addMidiFile ( const QString& sFilename )
{
	// Add the just dropped MIDI file...
	if (m_pFiles)
		m_pFiles->addMidiFile(sFilename);

	stabilizeForm();
}


// MIDI file selection slot funtion.
void qtractorMainForm::selectMidiFile (
	const QString& sFilename, int iTrackChannel, bool bSelect )
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::selectMidiFile(\"%s\", %d, %d)",
		sFilename.toUtf8().constData(), iTrackChannel, int(bSelect));
#endif

	// Select MIDI file track/channel...
	if (m_pTracks) {
		m_pTracks->trackView()->selectFile(
			qtractorTrack::Midi, sFilename, iTrackChannel, bSelect);
	}

	stabilizeForm();
}


// MIDI file activation slot funtion.
void qtractorMainForm::activateMidiFile (
	const QString& sFilename, int iTrackChannel )
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::activateMidiFile(\"%s\", %d)",
		sFilename.toUtf8().constData(), iTrackChannel);
#endif

	//
	// TODO: Activate the MIDI file track/channel...
	//

	stabilizeForm();
}


// Tracks view selection change slot.
void qtractorMainForm::trackSelectionChanged (void)
{
#ifdef CONFIG_DEBUG_0
	qDebug("qtractorMainForm::trackSelectionChanged()");
#endif

	// Select sync to mixer...
	if (m_pTracks && m_pMixer && m_pMixer->trackRack()) {
		qtractorMixerStrip *pStrip = NULL;
		qtractorTrack *pTrack = m_pTracks->trackList()->currentTrack();
		if (pTrack)
			pStrip = (m_pMixer->trackRack())->findStrip(pTrack->monitor());
		if (pStrip) {
			int wm = (pStrip->width() >> 1);
			(m_pMixer->trackRack())->ensureVisible(
				pStrip->pos().x() + wm, 0, wm, 0);
		}
		// Doesn't matter whether strip is null...
		(m_pMixer->trackRack())->setSelectedStrip(pStrip);
		// HACK: Set current session track for monitoring purposes...
		if (m_ui.trackAutoMonitorAction->isChecked())
			m_pSession->setCurrentTrack(pTrack);
	}

	stabilizeForm();
}


// Mixer view selection change slot.
void qtractorMainForm::mixerSelectionChanged (void)
{
#ifdef CONFIG_DEBUG_0
	qDebug("qtractorMainForm::mixerSelectionChanged()");
#endif

	// Select sync to tracks...
	if (m_pTracks && m_pMixer && m_pMixer->trackRack()) {
		int iTrack = -1;
		qtractorMixerStrip *pStrip = (m_pMixer->trackRack())->selectedStrip();
		if (pStrip && pStrip->track())
			iTrack = (m_pTracks->trackList())->trackRow(pStrip->track());
		(m_pTracks->trackList())->setCurrentTrackRow(iTrack);
	}

	stabilizeForm();
}


// Tracks view selection change slot.
void qtractorMainForm::selectionNotifySlot ( qtractorMidiEditor *pMidiEditor )
{
#ifdef CONFIG_DEBUG_0
	qDebug("qtractorMainForm::selectionNotifySlot()");
#endif

	// Read session edit-head/tails...
	unsigned long iEditHead = m_pSession->editHead();
	unsigned long iEditTail = m_pSession->editTail();

	// Track-view is due...
	if (m_pTracks) {
		m_pTracks->trackView()->setEditHead(iEditHead);
		m_pTracks->trackView()->setEditTail(iEditTail);
		if (pMidiEditor)
			m_pTracks->trackView()->clearClipSelect();
	}

	// Update editors edit-head/tails...
	QListIterator<qtractorMidiEditorForm *> iter(m_editors);
	while (iter.hasNext()) {
		qtractorMidiEditor *pEditor = (iter.next())->editor();
		if (pEditor != pMidiEditor) {
			pEditor->setEditHead(iEditHead, false);
			pEditor->setEditTail(iEditTail, false);
		}
	}

	// Normal status ahead...
	stabilizeForm();
}


// Clip editors update helper.
void qtractorMainForm::changeNotifySlot ( qtractorMidiEditor *pMidiEditor )
{
#ifdef CONFIG_DEBUG_0
	qDebug("qtractorMainForm::changeNotifySlot()");
#endif

	updateContents(pMidiEditor, true);
}


// Command update helper.
void qtractorMainForm::updateNotifySlot ( bool bRefresh )
{
#ifdef CONFIG_DEBUG_0
	qDebug("qtractorMainForm::updateNotifySlot(%d)", int(bRefresh));
#endif

	// Always reset any track view selection...
	// (avoid change/update notifications, again)
	if (m_pTracks)
		m_pTracks->trackView()->clearClipSelect();

	// Proceed as usual...
	updateContents(NULL, bRefresh);
}


// Common update helper.
void qtractorMainForm::updateContents (
	qtractorMidiEditor *pMidiEditor, bool bRefresh )
{
	// Maybe, just maybe, we've made things larger...
	m_pSession->updateTimeScale();
	m_pSession->updateSessionLength();

	// Refresh track-view?
	if (m_pTracks)
		m_pTracks->updateContents(bRefresh);

	// Update other editors contents...
	QListIterator<qtractorMidiEditorForm *> iter(m_editors);
	while (iter.hasNext()) {
		qtractorMidiEditor *pEditor = (iter.next())->editor();
		if (pEditor != pMidiEditor) {
			pEditor->updateTimeScale();
			pEditor->updateContents();
		}
	}

	// Notify who's watching...
	contentsChanged();
}


// Tracks view contents change slot.
void qtractorMainForm::contentsChanged (void)
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::contentsChanged()");
#endif

	// HACK: Force play-head position update...
	// m_iPlayHead = 0;
	m_pTempoCursor->clear();

	// Stabilize session toolbar widgets...
//	m_pTempoSpinBox->setTempo(m_pSession->tempo(), false);
//	m_pTempoSpinBox->setBeatsPerBar(m_pSession->beatsPerBar(), false);
//	m_pTempoSpinBox->setBeatDivisor(m_pSession->beatDivisor(), false);
	m_pSnapPerBeatComboBox->setCurrentIndex(
		qtractorTimeScale::indexFromSnap(m_pSession->snapPerBeat()));

	m_pThumbView->updateContents();

	++m_iDirtyCount;
	selectionNotifySlot(NULL);
}


// Tempo spin-box change slot.
void qtractorMainForm::transportTempoChanged (
	float fTempo, unsigned short iBeatsPerBar, unsigned short iBeatDivisor )
{
#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportTempoChanged(%g, %u, %u)",
		fTempo, iBeatsPerBar, iBeatDivisor);
#endif

	// Find appropriate node...
	qtractorTimeScale *pTimeScale = m_pSession->timeScale();
	qtractorTimeScale::Cursor& cursor = pTimeScale->cursor();
	qtractorTimeScale::Node *pNode = cursor.seekFrame(m_pSession->playHead());

	// Now, express the change as a undoable command...
	if (pNode->prev()) {
		m_pSession->execute(
			new qtractorTimeScaleUpdateNodeCommand(pTimeScale, pNode->frame,
				fTempo, 2, iBeatsPerBar, iBeatDivisor));
	} else {
		m_pSession->execute(
			new qtractorSessionTempoCommand(m_pSession,
				fTempo, 2, iBeatsPerBar, iBeatDivisor));
	}

	++m_iTransportUpdate;
}

void qtractorMainForm::transportTempoFinished (void)
{
	static int s_iTempoFinished = 0;
	if (s_iTempoFinished > 0)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportTempoFinished()");
#endif

	++s_iTempoFinished;
	m_pTempoSpinBox->clearFocus();
//	if (m_pTracks)
//		m_pTracks->trackView()->setFocus();
	--s_iTempoFinished;
}


// Snap-per-beat spin-box change slot.
void qtractorMainForm::snapPerBeatChanged ( int iSnap )
{
	// Avoid bogus changes...
	unsigned short iSnapPerBeat = qtractorTimeScale::snapFromIndex(iSnap);
	if (iSnapPerBeat == m_pSession->snapPerBeat())
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::snapPerBeatChanged(%u)", iSnapPerBeat);
#endif

	// No need to express this change as a undoable command...
	m_pSession->setSnapPerBeat(iSnapPerBeat);
}


// Real thing: the playhead has been changed manually!
void qtractorMainForm::transportTimeChanged ( unsigned long iPlayHead )
{
	if (m_iTransportUpdate > 0)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportTimeChanged(%lu)", iPlayHead);
#endif

	m_pSession->setPlayHead(iPlayHead);
	++m_iTransportUpdate;

	stabilizeForm();
}

void qtractorMainForm::transportTimeFinished (void)
{
	static int s_iTimeFinished = 0;
	if (s_iTimeFinished > 0)
		return;

#ifdef CONFIG_DEBUG
	qDebug("qtractorMainForm::transportTimeFinished()");
#endif

	++s_iTimeFinished;
	m_pTimeSpinBox->clearFocus();
//	if (m_pTracks)
//		m_pTracks->trackView()->setFocus();
	--s_iTimeFinished;
}


// Time format custom context menu.
void qtractorMainForm::transportTimeContextMenu ( const QPoint& pos )
{
	if (m_pOptions == NULL)
		return;

	QMenu menu(this);
	QAction *pAction;

	pAction = menu.addAction(tr("&Frames"));
	pAction->setCheckable(true);
	pAction->setChecked(m_pOptions->iDisplayFormat == 0);
	pAction->setData(0);
	
	pAction = menu.addAction(tr("&Time"));
	pAction->setCheckable(true);
	pAction->setChecked(m_pOptions->iDisplayFormat == 1);
	pAction->setData(1);

	pAction = menu.addAction(tr("&BBT"));
	pAction->setCheckable(true);
	pAction->setChecked(m_pOptions->iDisplayFormat == 2);
	pAction->setData(2);

	pAction = menu.exec(m_pTimeSpinBox->mapToGlobal(pos));
	if (pAction) {
		m_pOptions->iDisplayFormat = pAction->data().toInt();
		updateDisplayFormat();
	}

	stabilizeForm();
}


// Tempo-map custom context menu.
void qtractorMainForm::transportTempoContextMenu ( const QPoint& /*pos*/ )
{
	viewTempoMap();
}


// end of qtractorMainForm.cpp
