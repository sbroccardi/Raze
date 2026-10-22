/*
** mainloop.cpp
** Implements the main game loop
**
**---------------------------------------------------------------------------
** Copyright 2020 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/


// For TryRunTics the following applies:
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright 1999-2016 Randy Heit
// Copyright 2002-2020 Christoph Oelckers
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//		DOOM Network game communication and protocol,
//		all OS independent parts.
//
//-----------------------------------------------------------------------------


#include "c_cvars.h"
#include "i_time.h"
#include "d_net.h"
#include "gamecontrol.h"
#include "c_console.h"
#include "razemenu.h"
#include "i_system.h"
#include "raze_sound.h"
#include "raze_music.h"
#include "vm.h"
#include "gamestate.h"
#include "screenjob_.h"
#include "c_console.h"
#include "uiinput.h"
#include "v_video.h"
#include "palette.h"
#include "build.h"
#include "mapinfo.h"
#include "automap.h"
#include "statusbar.h"
#include "gamestruct.h"
#include "savegamehelp.h"
#include "v_draw.h"
#include "gamehud.h"
#include "wipe.h"
#include "i_interface.h"
#include "texinfo.h"
#include "texturemanager.h"
#include "gameinput.h"

CVAR(Bool, vid_activeinbackground, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, r_ticstability, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, vid_dontdowait, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
EXTERN_CVAR(Bool, cl_capfps)
CVAR(Bool, cl_resumesavegame, true, CVAR_ARCHIVE)
EXTERN_CVAR (Bool, vid_vsync)
EXTERN_CVAR (Int, vid_maxfps)

DCorePlayer* PlayerArray[MAXPLAYERS];

IMPLEMENT_CLASS(DCorePlayer, true, true)
IMPLEMENT_POINTERS_START(DCorePlayer)
IMPLEMENT_POINTER(actor)
IMPLEMENT_POINTERS_END

void MarkPlayers()
{
	GC::MarkArray(PlayerArray, MAXPLAYERS);
}

bool r_NoInterpolate;
int entertic;
int oldentertics;
int gametic;
int nextwipe = wipe_None;

FString savename;
FString BackupSaveGame;

void DoLoadGame(const char* name);

bool sendsave;
FString	savedescription;
FString	savegamefile;

//==========================================================================
//
// 
//
//==========================================================================

void G_BuildTiccmd(usercmd_t* cmd) 
{
	if (sendsave)
	{
		sendsave = false;
		Net_WriteInt8(DEM_SAVEGAME);
		Net_WriteString(savegamefile.GetChars());
		Net_WriteString(savedescription.GetChars());
		savegamefile = "";
	}
	*cmd = {};
	gameInput.getInput(cmd);
}

//==========================================================================
//
//
//
//==========================================================================
bool newGameStarted;

void NewGame(MapRecord* map, int skill, bool ns = false)
{
	gi->FreeLevelData();
	newGameStarted = true;
	ShowIntermission(nullptr, map, nullptr, [=](bool) { 
		gi->NewGame(map, skill, ns); 
		gameaction = ga_level;
		ResetStatusBar();
		gameInput.resetCrouchToggle();
		});
}

//==========================================================================
//
//
//
//==========================================================================

void G_Ticker()
{
	handleevents();

	// Todo: Migrate state changes to here instead of doing them ad-hoc
	while (gameaction != ga_nothing)
	{
		auto ga = gameaction;
		gameaction = ga_nothing;
		switch (ga)
		{
		case ga_autoloadgame:
			C_FlushDisplay();
			if (BackupSaveGame.IsNotEmpty() && cl_resumesavegame)
			{
				DoLoadGame(BackupSaveGame.GetChars());
			}
			else
			{
				g_nextmap = currentLevel;
				FX_StopAllSounds();
				S_SetReverb(0);
				NewGame(g_nextmap, -1);
				BackupSaveGame = "";
			}
			break;

		case ga_completed:
			FX_StopAllSounds();
			S_SetReverb(0);
			gi->LevelCompleted(g_nextmap, g_nextskill);
			break;

		case ga_nextlevel:
			gi->FreeLevelData();
			gameaction = ga_level;
			gi->NextLevel(g_nextmap, g_nextskill);
			ResetStatusBar();
			if (!isBlood()) M_Autosave();
			break;

		case ga_newgame:
			FX_StopAllSounds();
			[[fallthrough]];
		case ga_newgamenostopsound:
			DeleteScreenJob();
			S_SetReverb(0);
			C_FlushDisplay();
			BackupSaveGame = "";
			NewGame(g_nextmap, g_nextskill, ga == ga_newgamenostopsound);
			break;

		case ga_startup:
			Mus_Stop();
			FX_StopAllSounds();
			gi->FreeLevelData();
			gamestate = GS_STARTUP;
			break;

		case ga_mainmenu:
			FX_StopAllSounds();
			if (isBlood()) Mus_Stop();
			[[fallthrough]];
		case ga_mainmenunostopsound:
			gi->FreeLevelData();
			gamestate = GS_MENUSCREEN;
			M_StartControlPanel(ga == ga_mainmenu);
			M_SetMenu(NAME_MainMenu);
			break;

		case ga_creditsmenu:
			FX_StopAllSounds();
			gi->FreeLevelData();
			gamestate = GS_MENUSCREEN;
			M_StartControlPanel(false);
			M_SetMenu(NAME_MainMenu);
			M_SetMenu(NAME_CreditsMenu);
			break;

		case ga_savegame:
			G_DoSaveGame(true, false, savegamefile.GetChars(), savedescription.GetChars());
			gameaction = ga_nothing;
			savegamefile = "";
			savedescription = "";
			break;

		case ga_loadgame:
		case ga_loadgamehidecon:
		//case ga_autoloadgame:
			G_DoLoadGame();
			break;

		case ga_autosave:
			if (gamestate == GS_LEVEL && !newGameStarted) M_Autosave();
			newGameStarted = false;
			break;

		case ga_level:
			Net_ResetCommands(true);
			Net_SetWaiting();
			Net_ClearFifo();
			inputState.ClearAllInput();
			gameInput.Clear();
			gamestate = GS_LEVEL;
			return;

		case ga_intro:
			gamestate = GS_INTRO;
			break;

		case ga_intermission:
			gamestate = GS_CUTSCENE;
			break;

		case ga_fullconsole:
			C_FullConsole();
			Mus_Stop();
			gameaction = ga_nothing;
			break;

		case ga_endscreenjob:
			EndScreenJob();
			break;

			// for later
		// case ga_recordgame,			// start a new demo recording (later)
		// case ga_loadgameplaydemo,	// load a savegame and play a demo.

		default:
			break;
		}
		C_AdjustBottom();
	}

	// get commands
	const int curTic = gametic / TicDup;

	for (auto client : NetworkClients)
	{
		ticcmd_t* cmd = &PlayerArray[client]->cmd;
		usercmd_t* nextCmd = &ClientStates[client].Tics[curTic % BACKUPTICS].Command;
		PlayerArray[client]->lastcmd = *cmd;
		PlayerArray[client]->resetCameraAngles();

		RunPlayerCommands(client, curTic);
		cmd->ucmd = *nextCmd;
	}

	C_RunDelayedCommands();
	updatePauseStatus();

	switch (gamestate)
	{
	default:
	case GS_STARTUP:
		gi->Startup();
		break;

	case GS_LEVEL:
		gameupdatetime.Reset();
		gameupdatetime.Clock();
		gameInput.ResetInputSync();
		gi->Ticker();
		TickStatusBar();
		levelTextTime--;
		gameupdatetime.Unclock();
		break;

	case GS_MENUSCREEN:
	case GS_FULLCONSOLE:
		break;
	case GS_CUTSCENE:
	case GS_INTRO:
		if (ScreenJobTick())
		{
			// synchronize termination with the playsim.
			Net_WriteInt8(DEM_ENDSCREENJOB);
		}
		break;

	}
}


void DrawOverlays()
{
	NetUpdate(0);			// send out any new accumulation

	if (gamestate != GS_INTRO) // do not draw overlays on the intros
	{
		// Draw overlay elements
		CT_Drawer();
		C_DrawConsole();
		M_Drawer();
		FStat::PrintStat(twod);
	}
	DrawRateStuff();
}

//==========================================================================
//
// Display
//
//==========================================================================
CVAR(String, drawtile, "", 0)	// debug stuff. Draws the tile with the given number on top of thze HUD

void Display()
{
	if (screen == nullptr || (!AppActive && (screen->IsFullscreen() || !vid_activeinbackground)))
	{
		return;
	}
	
	FTexture* wipestart = nullptr;
	if (nextwipe != wipe_None)
	{
		wipestart = screen->WipeStartScreen();
	}

	screen->FrameTime = I_msTimeFS();
	tileUpdateAnimations();
	screen->BeginFrame();
	twodpsp.Clear();
	twodpsp.SetSize(screen->GetWidth(), screen->GetHeight());
	twodpsp.ClearClipRect();
	twod->Clear();
	//twod->SetSize(screen->GetWidth(), screen->GetHeight());
	twod->Begin(screen->GetWidth(), screen->GetHeight());
	twod->ClearClipRect();
	switch (gamestate)
	{
	case GS_MENUSCREEN:
	case GS_FULLCONSOLE:
		gi->DrawBackground();
		break;

	case GS_INTRO:
	case GS_CUTSCENE:
		ScreenJobDraw();
		break;

	case GS_LEVEL:
		if (gametic != 0)
		{
			screen->FrameTime = I_msTimeFS();
			screen->BeginFrame();
			screen->SetSceneRenderTarget(gl_ssao != 0);
			//updateModelInterpolation();
			gi->Render();
			DrawFullscreenBlends();
			drawMapTitle();
			break;
		}
		[[fallthrough]];

	default:
		twod->ClearScreen();
		break;
	}
	
	if (nextwipe == wipe_None)
	{
		DrawOverlays();
		if (drawtile[0])
		{
			auto tex = TexMan.CheckForTexture(drawtile, ETextureType::Any);
			if (!tex.isValid()) tex = tileGetTextureID(atoi(drawtile));
			if (tex.isValid())
			{
				auto tx = TexMan.GetGameTexture(tex, true);
				if (tx)
				{
					int width = (int)tx->GetDisplayWidth();
					int height = (int)tx->GetDisplayHeight();
					int dwidth, dheight;
					if (width > height)
					{
						dwidth = screen->GetWidth() / 4;
						dheight = height * dwidth / width;
					}
					else
					{
						dheight = screen->GetHeight() / 4;
						dwidth = width * dheight / height;
					}
					DrawTexture(twod, tx, 0, 0, DTA_DestWidth, dwidth, DTA_DestHeight, dheight, TAG_DONE);
				}
			}
		}
	}
	else
	{
		PerformWipe(wipestart, screen->WipeEndScreen(), nextwipe, true, DrawOverlays);
		nextwipe = wipe_None;
	}

	screen->Update();
}

//==========================================================================
//
// SignalHandler - the first signal ends the main loop, a second one exits right away.
//
//==========================================================================

static bool gameloop_abort = false;

void SignalHandler(int signal)
{
	if (gameloop_abort)
	{
		Printf("Received signal %d, exiting\n", signal);
		exit(0);
	}
	else
	{
		Printf("Received signal %d, shutting down\n", signal);
		gameloop_abort = true;
	}
}

//==========================================================================
//
// MainLoop - only returns after a shutdown signal, aside from exceptions being thrown.
//
//==========================================================================

void MainLoop ()
{
	int lasttic = 0;

	// Clamp the timer to TICRATE until the playloop has been entered.
	r_NoInterpolate = true;

	if (userConfig.CommandMap.IsNotEmpty())
	{
		auto maprecord = FindMapByName(userConfig.CommandMap.GetChars());
		if (maprecord == nullptr)
		{
			maprecord = SetupUserMap(userConfig.CommandMap.GetChars(), g_gameType & GAMEFLAG_DUKE? "dethtoll.mid" : nullptr);
		}
		userConfig.CommandMap = "";
		if (maprecord)
		{
			DeferredStartGame(maprecord, g_nextskill);
		}
	}

	while (!gameloop_abort)
	{
		try
		{
			// frame syncronous IO operations
			if (gametic > lasttic)
			{
				lasttic = gametic;
				I_StartFrame ();
			}
			I_SetFrameTime();

			// update the scale factor for unsynchronised input here.
			gameInput.UpdateInputScale();

			TryRunTics (); // will run at least one tic
			// Update display, next frame, with current state.
			I_StartTic();

			Display();
			Mus_UpdateMusic();		// must be at the end.
		}
		catch (CRecoverableError &error)
		{
			if (error.GetMessage ())
			{
				Printf (PRINT_BOLD, "\n%s\n", error.GetMessage());
			}
			gi->ErrorCleanup();
			M_ClearMenus();
			C_FullConsole();
			gameaction = ga_nothing;
		}
		catch (CVMAbortException &error)
		{
			error.MaybePrintMessage();
			Printf("%s", error.stacktrace.GetChars());
			gi->ErrorCleanup();
			twod->SetOffset(DVector2(0, 0));
			M_ClearMenus();
			C_FullConsole();
		}
	}
}

