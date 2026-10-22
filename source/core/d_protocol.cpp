/*
** d_protocol.cpp
**
** Basic network packet creation routines and simple IFF parsing
**
**---------------------------------------------------------------------------
**
** Copyright 1998-2016 Marisa Heit
** Copyright 2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Code written prior to 2026 is also licensed under:
**
** SPDX-License-Identifier: BSD-3-Clause
**
**---------------------------------------------------------------------------
**
*/


#include "d_protocol.h"
#include "d_net.h"
#include "i_protocol.h"
#include "cmdlib.h"
#include "serializer.h"

extern int gametic;

void UnpackUserCmd (usercmd_t& cmd, const usercmd_t *basis, TArrayView<uint8_t>& stream)
{
	uint8_t flags;
	usercmd_t* ucmd = &cmd;

	if (basis != NULL)
	{
		if (basis != ucmd)
		{
			memcpy (ucmd, basis, sizeof(usercmd_t));
		}
	}
	else
	{
		memset (ucmd, 0, sizeof(usercmd_t));
	}

	flags = ReadInt8 (stream);

	if (flags)
	{
		// We can support up to 29 buttons, using from 0 to 4 bytes to store them.
		if (flags & UCMDF_BUTTONS)
			ucmd->actions = ESyncBits::FromInt(ReadInt32(stream));
		if (flags & UCMDF_PITCH)
			ucmd->ang.Pitch = DAngle::fromDeg(ReadFloat(stream));
		if (flags & UCMDF_YAW)
			ucmd->ang.Yaw = DAngle::fromDeg(ReadFloat(stream));
		if (flags & UCMDF_FORWARDMOVE)
			ucmd->vel.X = ReadFloat(stream);
		if (flags & UCMDF_SIDEMOVE)
			ucmd->vel.Y = ReadFloat(stream);
		if (flags & UCMDF_UPMOVE)
			ucmd->vel.Z = ReadFloat(stream);
		if (flags & UCMDF_ROLL)
			ucmd->ang.Roll = DAngle::fromDeg(ReadFloat(stream));
	}
}

void PackUserCmd (const usercmd_t& cmd, const usercmd_t *basis, TArrayView<uint8_t>& stream)
{
	const usercmd_t* ucmd = &cmd;
	uint8_t flags = 0;
	auto packingBitsView = TArrayView(stream.Data(), 1);
	InputPacket blank;

	if (basis == NULL)
	{
		memset (&blank, 0, sizeof(blank));
		basis = &blank;
	}

	WriteInt8 (0, stream);			// Make room for the packing bits

	if (ucmd->actions != basis->actions)
	{
		flags |= UCMDF_BUTTONS;
		WriteInt32(ucmd->actions, stream);
	}
	if (ucmd->ang.Pitch != basis->ang.Pitch)
	{
		flags |= UCMDF_PITCH;
		WriteFloat ((float)ucmd->ang.Pitch.Degrees(), stream);
	}
	if (ucmd->ang.Yaw != basis->ang.Yaw)
	{
		flags |= UCMDF_YAW;
		WriteFloat ((float)ucmd->ang.Yaw.Degrees(), stream);
	}
	if (ucmd->vel.X != basis->vel.X)
	{
		flags |= UCMDF_FORWARDMOVE;
		WriteFloat ((float)ucmd->vel.X, stream);
	}
	if (ucmd->vel.Y != basis->vel.Y)
	{
		flags |= UCMDF_SIDEMOVE;
		WriteFloat ((float)ucmd->vel.Y, stream);
	}
	if (ucmd->vel.Z != basis->vel.Z)
	{
		flags |= UCMDF_UPMOVE;
		WriteFloat ((float)ucmd->vel.Z, stream);
	}
	if (ucmd->ang.Roll != basis->ang.Roll)
	{
		flags |= UCMDF_ROLL;
		WriteFloat ((float)ucmd->ang.Roll.Degrees(), stream);
	}

	// Write the packing bits
	WriteInt8 (flags, packingBitsView);
}

FSerializer &Serialize(FSerializer &arc, const char *key, ticcmd_t &cmd, ticcmd_t *def)
{
	if (arc.BeginObject(key))
	{
		arc("consistency", cmd.consistency)
			("ucmd", cmd.ucmd)
			.EndObject();
	}
	return arc;
}

FSerializer &Serialize(FSerializer &arc, const char *key, InputPacket &cmd, InputPacket *def)
{
	if (arc.BeginObject(key))
	{
		arc("actions", cmd.actions)
			("vel", cmd.vel)
			("ang", cmd.ang)
			.EndObject();
	}
	return arc;
}

void WriteUserCmdMessage (const usercmd_t& ucmd, const usercmd_t *basis, TArrayView<uint8_t>& stream)
{
	if (basis == NULL)
	{
		if (ucmd.actions != 0 || !ucmd.vel.isZero() || !ucmd.ang.isZero())
		{
			WriteInt8 (DEM_USERCMD, stream);
			PackUserCmd (ucmd, basis, stream);
			return;
		}
	}
	else if (ucmd.actions != basis->actions || ucmd.vel != basis->vel || ucmd.ang != basis->ang)
	{
		WriteInt8 (DEM_USERCMD, stream);
		PackUserCmd (ucmd, basis, stream);
		return;
	}

	WriteInt8 (DEM_EMPTYUSERCMD, stream);
}


// Reads through the user command without actually setting any of its info. Used to get the size
// of the command when getting the length of the stream.
void SkipUserCmdMessage(TArrayView<uint8_t>& stream)
{
	while (true)
	{
		const uint8_t type = ReadInt8(stream);
		if (type == DEM_USERCMD)
		{
			const int flags = ReadInt8(stream);
			size_t skip = 0;
			if (flags & UCMDF_PITCH)		skip += 4;
			if (flags & UCMDF_YAW)			skip += 4;
			if (flags & UCMDF_FORWARDMOVE)	skip += 4;
			if (flags & UCMDF_SIDEMOVE)		skip += 4;
			if (flags & UCMDF_UPMOVE)		skip += 4;
			if (flags & UCMDF_ROLL)			skip += 4;
			if (flags & UCMDF_BUTTONS)		skip += 4;
			AdvanceStream(stream, skip);
			break;
		}
		else if (type == DEM_EMPTYUSERCMD)
		{
			break;
		}
		else
		{
			Net_SkipCommand(type, stream);
		}
	}
}

void ReadUserCmdMessage(TArrayView<uint8_t>& stream, int player, int tic)
{
	const int ticMod = tic % BACKUPTICS;

	auto& curTic = ClientStates[player].Tics[ticMod];
	usercmd_t& ticCmd = curTic.Command;

	const uint8_t* start = stream.Data();

	// Skip until we reach the player command. Event data will get read off once the
	// tick is actually executed.
	int type;
	while ((type = ReadInt8(stream)) != DEM_USERCMD && type != DEM_EMPTYUSERCMD)
		Net_SkipCommand(type, stream);

	// Subtract a byte to account for the fact the stream head is now sitting on the
	// user command.
	curTic.Data.SetData(nullptr, 0u);
	curTic.Data.SetData(start, int(stream.Data() - start - 1));

	if (type == DEM_USERCMD)
	{
		UnpackUserCmd(ticCmd,
			tic > 0 ? &ClientStates[player].Tics[(tic - 1) % BACKUPTICS].Command : nullptr, stream);
	}
	else
	{
		if (tic > 0)
			memcpy(&ticCmd, &ClientStates[player].Tics[(tic - 1) % BACKUPTICS].Command, sizeof(ticCmd));
		else
			memset(&ticCmd, 0, sizeof(ticCmd));
	}
}

void RunPlayerCommands(int player, int tic)
{
	// We don't have the full command yet, so don't run it.
	if (gametic % TicDup)
		return;

	auto& data = ClientStates[player].Tics[tic % BACKUPTICS].Data;
	auto stream = data.GetTArrayView();
	if (stream.Size())
	{
		while (stream.Size() > 0)
			Net_DoCommand(ReadInt8(stream), stream, player);
	}
}

uint8_t *lenspot = nullptr;

// Write the header of an IFF chunk and leave space
// for the length field.
void StartChunk (int id, TArrayView<uint8_t>& stream)
{
	WriteInt32(id, stream);
	lenspot = stream.Data();
	AdvanceStream(stream, 4);
}

// Write the length field for the chunk and insert
// pad byte if the chunk is odd-sized.
void FinishChunk (TArrayView<uint8_t>& stream)
{
	if (!lenspot)
		return;

	int len = int(stream.Data() - lenspot - 4);
	auto lenSpotView = TArrayView<uint8_t>(lenspot, 4);
	WriteInt32 (len, lenSpotView);
	if (len & 1)
		WriteInt8 (0, stream);

	lenspot = NULL;
}

// Skip past an unknown chunk. stream should be
// pointing to the chunk's length field.
void SkipChunk (TArrayView<uint8_t>& stream)
{
	int len = ReadInt32(stream);
	AdvanceStream(stream, len + (len & 1));
}
