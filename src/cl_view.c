/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"
#include "vx_stuff.h"
#include "gl_model.h"
#include "teamplay.h"
#include "rulesets.h"
#include "utils.h"
#include "hud.h"
#include "hud_common.h"
#include "mvd_utils.h"
#include "r_matrix.h"
#include "pmove.h"

#ifdef X11_GAMMA_WORKAROUND
#include "tr_types.h"
#endif
#include "r_local.h"
#include "r_renderer.h"
#include "r_brushmodel.h"

/*
The view is allowed to move slightly from its true position for bobbing,
but if it exceeds 8 pixels linear distance (spherical, not box), the list of
entities sent from the server may not include everything in the pvs, especially
when crossing a water boudnary.
*/

static cvar_t cl_bob      = { "cl_bob",      "0"   };
static cvar_t cl_bobcycle = { "cl_bobcycle", "0.0" };
static cvar_t cl_bobup    = { "cl_bobup",    "0.0" };
static cvar_t cl_bobhead  = { "cl_bobhead",  "0"   };

cvar_t	cl_rollspeed = {"cl_rollspeed", "200"};
cvar_t	cl_rollangle = {"cl_rollangle", "0"};
cvar_t	cl_rollalpha = {"cl_rollalpha", "20", 0, Rulesets_OnChange_cl_rollalpha};
cvar_t	v_kicktime = {"v_kicktime", "0.0"};
cvar_t	v_kickroll = {"v_kickroll", "0.0"};
cvar_t	v_kickpitch = {"v_kickpitch", "0.0"};
cvar_t	v_gunkick = {"v_gunkick", "0"};
cvar_t	v_viewheight = {"v_viewheight", "0"};

cvar_t	cl_drawgun = {"r_drawviewmodel", "1"};
cvar_t	cl_drawgun_invisible = {"r_drawviewmodel_invisible", "0"};
cvar_t  r_nearclip = {"r_nearclip", "2", CVAR_RULESET_MAX | CVAR_RULESET_MIN, NULL, R_MINIMUM_NEARCLIP, R_MAXIMUM_FARCLIP, R_MINIMUM_NEARCLIP };
cvar_t	r_viewmodelsize = {"r_viewmodelSize", "1"};
cvar_t	r_viewmodeloffset = {"r_viewmodeloffset", ""};
cvar_t  r_viewpreselgun = {"r_viewpreselgun", "0"};
cvar_t	r_viewlastfired = {"r_viewmodellastfired", "0"};

void Change_v_idle (cvar_t *var, char *value, qbool *cancel);
cvar_t	v_iyaw_cycle = {"v_iyaw_cycle", "2", 0, Change_v_idle};
cvar_t	v_iroll_cycle = {"v_iroll_cycle", "0.5", 0, Change_v_idle};
cvar_t	v_ipitch_cycle = {"v_ipitch_cycle", "1", 0, Change_v_idle};
cvar_t	v_iyaw_level = {"v_iyaw_level", "0.3", 0, Change_v_idle};
cvar_t	v_iroll_level = {"v_iroll_level", "0.1", 0, Change_v_idle};
cvar_t	v_ipitch_level = {"v_ipitch_level", "0.3", 0, Change_v_idle};
cvar_t	v_idlescale = {"v_idlescale", "0", 0, Change_v_idle};

cvar_t	crosshair = {"crosshair", "4",};
cvar_t	crosshaircolor = {"crosshaircolor", "255 0 0", CVAR_COLOR};
cvar_t	crosshairsize	= {"crosshairsize", "1"};
cvar_t  cl_crossx = {"cl_crossx", "0"};
cvar_t  cl_crossy = {"cl_crossy", "0"};

// gamma updates are expensive in hw: update at lower fps, otherwise drops are severe
static cvar_t vid_hwgamma_fps = { "vid_hwgamma_fps", "60" };

// QW262: less flash grenade effect in demos
cvar_t cl_demoplay_flash       = { "cl_demoplay_flash",      "0.33" };

cvar_t v_contentblend          = { "v_contentblend",         "0.2" };
cvar_t v_damagecshift          = { "v_damagecshift",         "0.2" };
cvar_t v_quadcshift            = { "v_quadcshift",           "0.5" };
cvar_t v_suitcshift            = { "v_suitcshift",           "0.5" };
cvar_t v_ringcshift            = { "v_ringcshift",           "0.5" };
cvar_t v_pentcshift            = { "v_pentcshift",           "0.5" };
cvar_t v_dlightcshift          = { "v_dlightcshift",         "1" };
cvar_t v_dlightcolor           = { "v_dlightcolor",          "1" };
cvar_t v_dlightcshiftpercent   = { "v_dlightcshiftpercent",  "0.5" };

cvar_t v_bonusflash            = { "cl_bonusflash",          "0" };

float	v_dmg_time, v_dmg_roll, v_dmg_pitch;

frame_t			*view_frame;
player_state_t	view_message;

void Change_v_idle (cvar_t *var, char *value, qbool *cancel) {
	// Don't allow cheating in TF
	*cancel = (cl.teamfortress && cls.state >= ca_connected && cbuf_current != &cbuf_svc);
}

float V_CalcRoll (vec3_t angles, vec3_t velocity) {
	vec3_t right;
	float sign, side;

	AngleVectors (angles, NULL, right, NULL);
	side = DotProduct (velocity, right);
	sign = side < 0 ? -1 : 1;
	side = fabs(side);

	side = (side < cl_rollspeed.value) ? side * Ruleset_RollAngle() / cl_rollspeed.value : Ruleset_RollAngle();

	if (side > 45)
		side = 45;

	return side * sign;
	
}

float V_CalcBob (void) {
	static double bobtime;
	static float bob;
	float cycle;
	
	if (cl.spectator)
		return 0;

	if (!cl.onground)
		return bob;		// just use old value

	if (cl_bobcycle.value <= 0)	
		return 0;

	bobtime += cls.frametime;
	cycle = bobtime - (int) (bobtime / cl_bobcycle.value) * cl_bobcycle.value;
	cycle /= cl_bobcycle.value;
	if (cycle < cl_bobup.value)
		cycle = M_PI * cycle / cl_bobup.value;
	else
		cycle = M_PI + M_PI * (cycle - cl_bobup.value) / (1.0 - cl_bobup.value);

	// bob is proportional to simulated velocity in the xy plane
	// (don't count Z, or jumping messes it up)
	bob = sqrt(cl.simvel[0] * cl.simvel[0] + cl.simvel[1] * cl.simvel[1]) * cl_bob.value;
	bob = bob * 0.3 + bob * 0.7 * sin(cycle);
	bob = bound (-7, bob, 4);
	return bob;	
}

//=============================================================================

cvar_t	v_centermove = {"v_centermove", "0.15"};
cvar_t	v_centerspeed = {"v_centerspeed","500"};

void Force_Centerview_f (void)
{
	if (concussioned)
		return;

	cl.viewangles[PITCH] = 0;
}

void V_StartPitchDrift (void) {
	if (cl.laststop == cl.time)
		return;		// something else is keeping it from drifting

	if (cl.nodrift || !cl.pitchvel) {
		cl.pitchvel = v_centerspeed.value;
		cl.nodrift = false;
		cl.driftmove = 0;
	}
}

void V_StopPitchDrift (void) {
	cl.laststop = cl.time;
	cl.nodrift = true;
	cl.pitchvel = 0;
}

/*
Moves the client pitch angle towards cl.idealpitch sent by the server.

If the user is adjusting pitch manually, either with lookup/lookdown,
mlook and mouse, or klook and keyboard, pitch drifting is constantly stopped.

Drifting is enabled when the center view key is hit, mlook is released and
lookspring is non 0, or when 
*/
void V_DriftPitch (void) {
	float delta, move;

	if (!cl.onground || cls.demoplayback ) {
		cl.driftmove = cl.pitchvel = 0;
		return;
	}

// don't count small mouse motion
	if (cl.nodrift) {
		if (abs(cl.frames[(cls.netchan.outgoing_sequence-1)&UPDATE_MASK].cmd.forwardmove) < 200)
			cl.driftmove = 0;
		else
			cl.driftmove += cls.frametime;
	
		if ( cl.driftmove > v_centermove.value)
			V_StartPitchDrift ();

		return;
	}
	
	delta = 0 - cl.viewangles[PITCH];

	if (!delta) {
		cl.pitchvel = 0;
		return;
	}

	move = cls.frametime * cl.pitchvel;
	cl.pitchvel += cls.frametime * v_centerspeed.value;

	if (delta > 0) {
		if (move > delta) {
			cl.pitchvel = 0;
			move = delta;
		}
		cl.viewangles[PITCH] += move;
	} else if (delta < 0) {
		if (move > -delta) {
			cl.pitchvel = 0;
			move = -delta;
		}
		cl.viewangles[PITCH] -= move;
	}
}

/*
============================================================================== 
 						PALETTE FLASHES 
============================================================================== 
*/
 
cshift_t	cshift_empty = { {130,80,50}, 0 };
cshift_t	cshift_water = { {130,80,50}, 128 };
cshift_t	cshift_slime = { {0,25,5}, 150 };
cshift_t	cshift_lava = { {255,80,0}, 150 };

cvar_t		gl_cshiftpercent = {"gl_cshiftpercent", "100"};
cvar_t		gl_hwblend = {"gl_hwblend", "0"};
float		v_blend[4];		// rgba 0.0 - 1.0
cvar_t		v_gamma = {"gl_gamma", "1.0"};
cvar_t		v_contrast = {"gl_contrast", "1.0"};

#ifdef X11_GAMMA_WORKAROUND
unsigned short ramps[3][4096];
#else
unsigned short	ramps[3][256];
#endif

void V_ParseDamage (void)
{
	int armor, blood, i;
	vec3_t from, forward, right;
	float side, count, fraction;

	armor = MSG_ReadByte ();
	blood = MSG_ReadByte ();
	for (i = 0; i < 3; i++)
		from[i] = MSG_ReadCoord ();

	if (cls.mvdplayback && cls.lastto >= 0 && cls.lastto < MAX_CLIENTS) {
		cl.players[cls.lastto].max_health_last_set = cls.demotime;
	}

	if (CL_Demo_SkipMessage(true))
		return;

	count = blood * 0.5 + armor * 0.5;
	if (count < 10)
		count = 10;

	cl.faceanimtime = cl.time + 0.2;		// put sbar face into pain frame

	cl.hurtblur = cl.time + count / 24;		// use hurt motion blur.

	cl.cshifts[CSHIFT_DAMAGE].percent += 3*count;
	if (cl.cshifts[CSHIFT_DAMAGE].percent < 0)
		cl.cshifts[CSHIFT_DAMAGE].percent = 0;
	if (cl.cshifts[CSHIFT_DAMAGE].percent > 150)
		cl.cshifts[CSHIFT_DAMAGE].percent = 150;

	fraction = bound(0, v_damagecshift.value, 1);
	cl.cshifts[CSHIFT_DAMAGE].percent *= fraction;

	if (armor > blood) {
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 200;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 100;
	} else if (armor) {
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 220;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 50;
	} else {
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 255;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 0;
	}

	// calculate view angle kicks
	VectorSubtract (from, cl.simorg, from);
	VectorNormalizeFast (from);

	AngleVectors (cl.simangles, forward, right, NULL);

	side = DotProduct (from, right);
	v_dmg_roll = count * side * v_kickroll.value;

	side = DotProduct (from, forward);
	v_dmg_pitch = count * side * v_kickpitch.value;

	v_dmg_time = v_kicktime.value;
}

// disconnect -->
qbool flashed = false; // it should be used for f_flashout tirgger
extern cvar_t v_gamma, v_contrast;

void V_TF_FlashSettings(qbool flashed)
{
	static float old_gamma, old_contrast;

	// remove read only flag if it was set
	if (Cvar_GetFlags(&v_gamma) & CVAR_ROM) {
		Cvar_SetFlags(&v_gamma, Cvar_GetFlags(&v_gamma) & ~CVAR_ROM);
	}
	if (Cvar_GetFlags(&v_contrast) & CVAR_ROM) {
		Cvar_SetFlags(&v_contrast, Cvar_GetFlags(&v_contrast) & ~CVAR_ROM);
	}

	if (flashed) {
		// store normal settings
		old_gamma = v_gamma.value;
		old_contrast = v_contrast.value;

		// set MTFL flash settings	
		Cvar_SetValue(&v_gamma, MTFL_FLASH_GAMMA);
		Cvar_SetValue(&v_contrast, MTFL_FLASH_CONTRAST);

		// made gamma&contrast read only
		Cvar_SetFlags(&v_gamma, Cvar_GetFlags(&v_gamma) | CVAR_ROM);
		Cvar_SetFlags(&v_contrast, Cvar_GetFlags(&v_contrast) | CVAR_ROM);
	}
	else {
		// restore old settings
		Cvar_SetValue(&v_gamma, old_gamma);
		Cvar_SetValue(&v_contrast, old_contrast);
	}
}

void V_TF_FlashStuff (void)
{
	static float last_own_flash_time;
	static float last_other_flash_time;
	float blocktime;

	// 240 = Normal TF || 255 = Angel TF
	if (cshift_empty.percent == 240 || cshift_empty.percent == 255 ) {
		TP_ExecTrigger ("f_flash");
		if (!flashed && Rulesets_ToggleWhenFlashed()) {
			V_TF_FlashSettings(true);
		}

		flashed = true;
		last_other_flash_time = cls.realtime;
	}

	if (cshift_empty.percent == 160) {
		// flashed by your own flash
		if (!flashed && Rulesets_ToggleWhenFlashed()) {
			V_TF_FlashSettings(true);
		}

		flashed = true;
		last_own_flash_time = cls.realtime;
	}

	blocktime = (last_other_flash_time > last_own_flash_time) ? 20.0 : 10.0;

	{
		qbool flashed_for_10seconds = (!(cls.realtime - max(last_own_flash_time, last_other_flash_time) < blocktime));
		qbool death_while_flashed = (cshift_empty.percent == 0 && cbuf_current == &cbuf_svc);

		if (flashed_for_10seconds || death_while_flashed) {
			// turn gamma and contrast back
			if (flashed && Rulesets_ToggleWhenFlashed()) {
				V_TF_FlashSettings(false);
			}
			flashed = false;
		}
	}

	if (cls.demoplayback && cshift_empty.destcolor[0] == cshift_empty.destcolor[1]) {
		cshift_empty.percent *= bound(0, cl_demoplay_flash.value, 1.0f);
	}
}
// <-- disconnect

void V_cshift_f (void) {
	// don't allow cheating in TF
	if (cls.state >= ca_connected && cl.teamfortress && cbuf_current != &cbuf_svc)
		return;

	cshift_empty.destcolor[0] = atoi(Cmd_Argv(1));
	cshift_empty.destcolor[1] = atoi(Cmd_Argv(2));
	cshift_empty.destcolor[2] = atoi(Cmd_Argv(3));
	cshift_empty.percent = atoi(Cmd_Argv(4));

	// TF flash grenades stuff
	if (cl.teamfortress)
		V_TF_FlashStuff ();
}

//When you run over an item, the server sends this command
void V_BonusFlash_f (void) {
        static double last_bonusflashtrigger = 0;

	if (cls.realtime != last_bonusflashtrigger) { // do not trigger twice a frame
		TP_ExecTrigger ("f_bonusflash");
		last_bonusflashtrigger = cls.realtime;
	}

	if (!v_bonusflash.value && cbuf_current == &cbuf_svc)
		return;

	cl.cshifts[CSHIFT_BONUS].destcolor[0] = 215;
	cl.cshifts[CSHIFT_BONUS].destcolor[1] = 186;
	cl.cshifts[CSHIFT_BONUS].destcolor[2] = 69;
	cl.cshifts[CSHIFT_BONUS].percent = 50;
}

//Underwater, lava, etc each has a color shift
void V_SetContentsColor (int contents)
{
	extern cvar_t gl_polyblend;

	if (!v_contentblend.value) {
		cl.cshifts[CSHIFT_CONTENTS] = cshift_empty;
		cl.cshifts[CSHIFT_CONTENTS].percent *= 100;
		return;
	}

	switch (contents) {
		case CONTENTS_EMPTY:
			cl.cshifts[CSHIFT_CONTENTS] = cshift_empty;
			break;
		case CONTENTS_LAVA:
			cl.cshifts[CSHIFT_CONTENTS] = cshift_lava;
			break;
		case CONTENTS_SOLID:
		case CONTENTS_SLIME:
			cl.cshifts[CSHIFT_CONTENTS] = cshift_slime;
			break;
		default:
			cl.cshifts[CSHIFT_CONTENTS] = cshift_water;
			break;
	}

	if (v_contentblend.value > 0 && v_contentblend.value < 1 && contents != CONTENTS_EMPTY) {
		cl.cshifts[CSHIFT_CONTENTS].percent *= v_contentblend.value;
	}

	if (!gl_polyblend.value && !cl.teamfortress) {
		cl.cshifts[CSHIFT_CONTENTS].percent = 0;
	}
	else {
		// ignore gl_cshiftpercent on custom cshifts (set with v_cshift
		// command) to avoid cheating in TF
		if (contents != CONTENTS_EMPTY) {
			if (!gl_polyblend.value) {
				cl.cshifts[CSHIFT_CONTENTS].percent = 0;
			}
			else {
				cl.cshifts[CSHIFT_CONTENTS].percent *= gl_cshiftpercent.value;
			}
		}
		else {
			cl.cshifts[CSHIFT_CONTENTS].percent *= 100;
		}
	}
}

void V_CalcPowerupCshift(void)
{
	float fraction;

	if (cl.stats[STAT_ITEMS] & IT_QUAD)	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 255;
		fraction = bound (0, v_quadcshift.value, 1);
		cl.cshifts[CSHIFT_POWERUP].percent = 30 * fraction;
	} else if (cl.stats[STAT_ITEMS] & IT_SUIT) {
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
		fraction = bound (0, v_suitcshift.value, 1);
		cl.cshifts[CSHIFT_POWERUP].percent = 20 * fraction;
	} else if (cl.stats[STAT_ITEMS] & IT_INVISIBILITY) {
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 100;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 100;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 100;
		fraction = bound (0, v_ringcshift.value, 1);
		cl.cshifts[CSHIFT_POWERUP].percent = 100 * fraction;
	} else if (cl.stats[STAT_ITEMS] & IT_INVULNERABILITY) {
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
		fraction = bound (0, v_pentcshift.value, 1);
		cl.cshifts[CSHIFT_POWERUP].percent = 30 * fraction;
	} else {
		cl.cshifts[CSHIFT_POWERUP].percent = 0;
	}
}

void V_CalcBlend (void)
{
	float r, g, b, a, a2, t;
	int j;
	extern cvar_t gl_polyblend;

	r = g = b = a = 0;

	if (cls.state != ca_active) {
		cl.cshifts[CSHIFT_CONTENTS] = cshift_empty;
		cl.cshifts[CSHIFT_POWERUP].percent = 0;
	}
	else {
		V_CalcPowerupCshift ();
	}

	// drop the damage value
	t = cls.frametime * 150;
	cl.cshifts[CSHIFT_DAMAGE].percent -= t;
	if (cl.cshifts[CSHIFT_DAMAGE].percent <= 0) {
		cl.cshifts[CSHIFT_DAMAGE].percent = 0;
	}

	// drop the bonus value
	cl.cshifts[CSHIFT_BONUS].percent -= cls.frametime * 100;
	if (cl.cshifts[CSHIFT_BONUS].percent <= 0) {
		cl.cshifts[CSHIFT_BONUS].percent = 0;
	}

	for (j = 0; j < NUM_CSHIFTS; j++)	{
		if ((!gl_cshiftpercent.value || !gl_polyblend.value) && j != CSHIFT_CONTENTS) {
			continue;
		}

		if (j == CSHIFT_CONTENTS) {
			a2 = cl.cshifts[j].percent / 100.0 / 255.0;
		}
		else {
			a2 = ((cl.cshifts[j].percent * gl_cshiftpercent.value) / 100.0) / 255.0;
		}

		if (!a2) {
			continue;
		}
		a = a + a2 * (1-a);

		a2 = a2 / a;
		r = r * (1 - a2) + cl.cshifts[j].destcolor[0] * a2;
		g = g * (1 - a2) + cl.cshifts[j].destcolor[1] * a2;
		b = b * (1 - a2) + cl.cshifts[j].destcolor[2 ]* a2;
	}

	v_blend[0] = r / 255.0;
	v_blend[1] = g / 255.0;
	v_blend[2] = b / 255.0;
	v_blend[3] = a;
	v_blend[3] = bound(0, v_blend[3], 1);
}

void V_AddLightBlend(float r, float g, float b, float a2, qbool suppress_polyblend)
{
	float a;
	extern cvar_t gl_polyblend;
	qbool shift_on_dlight = gl_polyblend.integer && (v_dlightcshift.integer == 1 || (v_dlightcshift.integer == 2 && !suppress_polyblend));
	float percentage = bound(0, v_dlightcshiftpercent.value, 1);

	if (percentage <= 0 || !shift_on_dlight) {
		return;
	}

	if (!v_dlightcolor.integer) {
		r = 1.0f;
		g = 0.5f;
		b = 0.0f;
	}
	else {
		// some kind of scaling, the normal colors aren't full red/blue etc
		float max = max(r, max(g, b));

		if (max > 0) {
			r /= max;
			g /= max;
			b /= max;
		}
	}

	a2 = bound(0, a2, 1);
	a2 *= percentage;

	v_blend[3] = a = v_blend[3] + a2 * (1 - v_blend[3]);

	if (!a) {
		return;
	}

	a2 = a2 / a;

	v_blend[0] = v_blend[0] * (1 - a2) + r * a2;
	v_blend[1] = v_blend[1] * (1 - a2) + g * a2;
	v_blend[2] = v_blend[2] * (1 - a2) + b * a2;
}

void V_UpdatePalette (void)
{
	int i, j, c;
	float current_gamma, current_contrast, a, rgb[3];
	static float prev_blend[4] = { 0, 0, 0, 0 };
	static float old_gamma = 1, old_contrast = 1;
	static qbool old_change_palette = false;
	static double last_set = 0;
	extern float vid_gamma;
	float hw_vblend[4];
	// whether or not to include content/damage palette shifts as part of the hw gamma
	qbool change_palette = (vid_hwgamma_enabled && gl_hwblend.value && !cl.teamfortress);
	int change_flags = 0;

	if (change_palette) {
		memcpy(hw_vblend, v_blend, sizeof(hw_vblend));
	}
	else {
		memset(hw_vblend, 0, sizeof(hw_vblend));
	}
	current_gamma = (vid_hwgamma_enabled ? bound(0.3, v_gamma.value, 3) : 1);
	current_contrast = (vid_hwgamma_enabled ? bound(1, v_contrast.value, 3) : 1);

	change_flags |= (memcmp(hw_vblend, prev_blend, sizeof(hw_vblend)) ? 1 : 0);
	change_flags |= (current_gamma != old_gamma || v_gamma.modified ? 2 : 0);
	change_flags |= (current_contrast != old_contrast ? 4 : 0);
	change_flags |= (change_palette != old_change_palette ? 8 : 0);

	if (!change_flags) {
		return;
	}

	// don't update if not enough time has passed & only palette updating
	if (change_flags == 1 && vid_hwgamma_fps.integer && curtime < last_set + (1.0f / max(10, vid_hwgamma_fps.integer))) {
		return;
	}

	// store flags
	old_change_palette = change_palette;
	prev_blend[0] = hw_vblend[0];
	prev_blend[1] = hw_vblend[1];
	prev_blend[2] = hw_vblend[2];
	prev_blend[3] = hw_vblend[3];
	old_gamma = current_gamma;
	old_contrast = current_contrast;
	last_set = curtime;
	v_gamma.modified = false;
	if (developer.integer == 3) {
		Con_DPrintf("palette: change_flags %d [%d %d %d %d] %.2f %.2f\n", change_flags, (int)(hw_vblend[0] * 255), (int)(hw_vblend[1] * 255), (int)(hw_vblend[2] * 255), (int)(hw_vblend[3] * 255), current_gamma, current_contrast);
	}

	a = hw_vblend[3];
	if (R_OldGammaBehaviour() && vid_gamma != 1.0) {
		current_contrast = pow(current_contrast, vid_gamma);
		current_gamma = current_gamma / vid_gamma;
	}

	// Have to do this in a loop these days as certain color ranges will be blocked by OS
	do {
		float std_alpha;

		rgb[0] = 255 * hw_vblend[0] * a;
		rgb[1] = 255 * hw_vblend[1] * a;
		rgb[2] = 255 * hw_vblend[2] * a;

		std_alpha = 1 - a;

#ifdef X11_GAMMA_WORKAROUND
		std_alpha *= 256.0 / glConfig.gammacrap.size;
		for (i = 0; i < glConfig.gammacrap.size; i++) {
#else
		for (i = 0; i < 256; i++) {
#endif
			for (j = 0; j < 3; j++) {
				// apply blend and contrast
				c = (i * std_alpha + rgb[j]) * current_contrast;
				if (c > 255) {
					c = 255;
				}
				// apply gamma
				c = 255 * pow(c / 255.5, current_gamma) + 0.5;
				c = bound(0, c, 255);
				ramps[j][i] = c << 8;
			}
		}

		a *= 0.8;
	} while (VID_SetDeviceGammaRamp((unsigned short *)ramps) && a > 0.1);
}

// BorisU -->
void V_TF_ClearGrenadeEffects (void)
{
	cbuf_t *cbuf_tmp;
	extern cvar_t scr_fov, default_fov;
	
	cbuf_tmp = cbuf_current;
	cbuf_current = &cbuf_svc;
	// Concussion effect off
	concussioned = false;
	Cvar_SetValue (&scr_fov, default_fov.value);
	Cvar_SetValue (&v_idlescale, 0.0f);

	// Flash effect off
	if (flashed && Rulesets_ToggleWhenFlashed()) {
		V_TF_FlashSettings (false);
	}
	flashed = false;

	cshift_empty.destcolor[0] = 0;
	cshift_empty.destcolor[1] = 0;
	cshift_empty.destcolor[2] = 0;
	cshift_empty.percent = 0;
	cbuf_current = cbuf_tmp;
}
// <-- BorisU
/* 
============================================================================== 
						         VIEW RENDERING 
============================================================================== 
*/

float angledelta (float a) {
	a = anglemod(a);
	if (a > 180)
		a -= 360;
	return a;
}

void V_BoundOffsets (void) {
	// absolutely bound refresh relative to entity clipping hull
	// so the view can never be inside a solid wall

	if (r_refdef.vieworg[0] < cl.simorg[0] - 14)
		r_refdef.vieworg[0] = cl.simorg[0] - 14;
	else if (r_refdef.vieworg[0] > cl.simorg[0] + 14)
		r_refdef.vieworg[0] = cl.simorg[0] + 14;

	if (r_refdef.vieworg[1] < cl.simorg[1] - 14)
		r_refdef.vieworg[1] = cl.simorg[1] - 14;
	else if (r_refdef.vieworg[1] > cl.simorg[1] + 14)
		r_refdef.vieworg[1] = cl.simorg[1] + 14;

	if (r_refdef.vieworg[2] < cl.simorg[2] - 22)
		r_refdef.vieworg[2] = cl.simorg[2] - 22;
	else if (r_refdef.vieworg[2] > cl.simorg[2] + 30)
		r_refdef.vieworg[2] = cl.simorg[2] + 30;
}

//Idle swaying
void V_AddIdle (void) {
	r_refdef.viewangles[ROLL] += v_idlescale.value * sin(cl.time * v_iroll_cycle.value) * v_iroll_level.value;
	r_refdef.viewangles[PITCH] += v_idlescale.value * sin(cl.time * v_ipitch_cycle.value) * v_ipitch_level.value;
	r_refdef.viewangles[YAW] += v_idlescale.value * sin(cl.time * v_iyaw_cycle.value) * v_iyaw_level.value;
}

//Roll is induced by movement and damage
void V_CalcViewRoll (void) {
	float side, adjspeed;

	side = V_CalcRoll (cl.simangles, cl.simvel);
	adjspeed = cl_rollalpha.value * bound (2, Ruleset_RollAngle(), 45);
	if (side > cl.rollangle) {
		cl.rollangle += cls.frametime * adjspeed;
		if (cl.rollangle > side)
			cl.rollangle = side;
	} else if (side < cl.rollangle) {
		cl.rollangle -= cls.frametime * adjspeed;
		if (cl.rollangle < side)
			cl.rollangle = side;
	}
	r_refdef.viewangles[ROLL] += cl.rollangle;

	if (v_dmg_time > 0) {
		r_refdef.viewangles[ROLL] += v_dmg_time/v_kicktime.value*v_dmg_roll;
		r_refdef.viewangles[PITCH] += v_dmg_time/v_kicktime.value*v_dmg_pitch;
		v_dmg_time -= cls.frametime;
	}
}

// tells the model number of the current carried/selected/preselected weapon
// if user wish so, weapon pre-selection is also taken in account
// todo: if user selects different weapon while the current one is still
// firing, wait until the animation is finished
static int V_CurrentWeaponModel(void)
{
	extern cvar_t cl_weaponpreselect;
	int bestgun;
	int realw = cl.stats[STAT_WEAPON];

	if (cls.demoplayback && r_viewlastfired.integer) {
		if (realw == 0) {
			cl.lastfired = realw;
			cl.lastviewplayernum = cl.viewplayernum;
			return realw;
		}
		if (view_message.weaponframe) {
			cl.lastfired = realw;
			cl.lastviewplayernum = cl.viewplayernum;
			return realw;
		}
		else if (cl.lastfired) {
			if (cl.lastviewplayernum == cl.viewplayernum) {
				return cl.lastfired;
			}
			else {
				cl.lastfired = realw;
				cl.lastviewplayernum = cl.viewplayernum;
				return realw;
			}
		}
		else {
			return realw;
		}
	}
	else {
		if (ShowPreselectedWeap() && r_viewpreselgun.integer && !view_message.weaponframe) {
			bestgun = IN_BestWeaponReal(true);
			if (bestgun == 1) {
				return cl_modelindices[mi_vaxe];
			}
			if (bestgun > 1 && bestgun <= 8) {
				if (!pmove_nopred_weapon && pmove.client_predflags & PRDFL_COILGUN)
					if (bestgun == 2) { bestgun = 9; }

				return cl_modelindices[mi_weapon1 - 1 + bestgun];
			}
		}
		else if (!pmove_nopred_weapon && cls.mvdprotocolextensions1 & MVD_PEXT1_WEAPONPREDICTION) {
			if (cl.simwep == 1)
				return cl_modelindices[mi_vaxe];
			else if (cl.simwep > 1 && cl.simwep <= 9)
				return cl_modelindices[mi_weapon1 - 1 + cl.simwep];
		}
		return cl.stats[STAT_WEAPON];
	}
}

extern void TP_ParseWeaponModel(model_t *model);

static void V_AddViewWeapon(float bob)
{
	vec3_t forward, right, up;
	centity_t *cent;
	int gunmodel = V_CurrentWeaponModel();
	extern cvar_t scr_fov, scr_fovmode, scr_newHud;

	cent = CL_WeaponModelForView();
	TP_ParseWeaponModel(cl.model_precache[gunmodel]);

	if (!cl_drawgun.value
		|| (cl_drawgun.value == 2 && scr_fov.value > 90)
		|| ((view_message.flags & (PF_GIB | PF_DEAD)))
		|| (!cl_drawgun_invisible.value && cl.stats[STAT_ITEMS] & IT_INVISIBILITY)
		|| cl.stats[STAT_HEALTH] <= 0
		|| !Cam_DrawViewModel()) {
		cent->current.modelindex = 0;	//no model
		return;
	}

	//angles
	cent->current.angles[YAW] = r_refdef.viewangles[YAW];
	cent->current.angles[PITCH] = -r_refdef.viewangles[PITCH];
	cent->current.angles[ROLL] = r_refdef.viewangles[ROLL];
	//origin

	AngleVectors(r_refdef.viewangles, forward, right, up);
	VectorCopy(r_refdef.vieworg, cent->current.origin);

	VectorMA(cent->current.origin, bob * 0.4, forward, cent->current.origin);

	if (r_viewmodeloffset.string[0]) {
		float offset[3];
		int size = sizeof(offset) / sizeof(offset[0]);

		ParseFloats(r_viewmodeloffset.string, offset, &size);
		VectorMA(cent->current.origin, offset[0], right, cent->current.origin);
		VectorMA(cent->current.origin, -offset[1], up, cent->current.origin);
		VectorMA(cent->current.origin, offset[2], forward, cent->current.origin);
	}

	// fudge position around to keep amount of weapon visible roughly equal with different FOV
	if (!scr_fovmode.value) {
		if (cl_sbar.value && scr_newHud.value == 0 && scr_viewsize.value == 110)
			cent->current.origin[2] += 1;
		else if (cl_sbar.value && scr_newHud.value == 0 && scr_viewsize.value == 100)
			cent->current.origin[2] += 2;
		else if (scr_viewsize.value == 90)
			cent->current.origin[2] += 1;
		else if (scr_viewsize.value == 80)
			cent->current.origin[2] += 0.5;
	}

	if (!pmove_nopred_weapon && cls.mvdprotocolextensions1 & MVD_PEXT1_WEAPONPREDICTION)
		view_message.weaponframe = cl.simwepframe;

	if (cent->current.modelindex != gunmodel) {
		cent->frametime = -1;
	}
	else {
		if (cent->current.frame != view_message.weaponframe) {
			cent->frametime = cl.time;
			cent->oldframe = cent->current.frame;
		}
	}

	cent->current.modelindex = gunmodel;
	cent->current.frame = view_message.weaponframe;
}

static void V_CalcIntermissionRefdef(void)
{
	float old;

	VectorCopy (cl.simorg, r_refdef.vieworg);
	VectorCopy (cl.simangles, r_refdef.viewangles);

	// we don't draw weapon in intermission
	CL_WeaponModelForView()->current.modelindex = 0;

	// always idle in intermission
	old = v_idlescale.value;
	v_idlescale.value = 1;
	V_AddIdle ();
	v_idlescale.value = old;
}

static void V_CalcRefdef(void)
{
	vec3_t forward;
	float bob;
	float height_adjustment;

	V_DriftPitch();

	bob = V_CalcBob();

	height_adjustment = v_viewheight.value ? bound(-7, v_viewheight.value, 4) : V_CalcBob();
	if (cl_bobhead.integer) {
		height_adjustment += bob;
		bob = 0;
	}

	// set up the refresh position
	VectorCopy(cl.simorg, r_refdef.vieworg);

	// never let it sit exactly on a node line, because a water plane can
	// dissapear when viewed with the eye exactly on it.
	// the server protocol only specifies to 1/8 pixel, so add 1/16 in each axis
	r_refdef.vieworg[0] += 1.0 / 16;
	r_refdef.vieworg[1] += 1.0 / 16;
	r_refdef.vieworg[2] += 1.0 / 16;

	// add view height
	r_refdef.viewheight_test = 4;
	if (view_message.flags & PF_GIB) {
		r_refdef.vieworg[2] += 8;	// gib view height
	}
	else if (view_message.flags & PF_DEAD && (cl.stats[STAT_HEALTH] <= 0)) {
		r_refdef.vieworg[2] -= 16;	// corpse view height
	}
	else {
		// normal view height
		// Use STAT_VIEWHEIGHT in case of server support it or NQ demoplayback, if not then use default viewheight.
		r_refdef.vieworg[2] += ((cl.z_ext & Z_EXT_VIEWHEIGHT) || cls.nqdemoplayback) ? cl.stats[STAT_VIEWHEIGHT] : DEFAULT_VIEWHEIGHT;

		r_refdef.vieworg[2] += height_adjustment;

		// smooth out stair step ups
		r_refdef.vieworg[2] += cl.crouch;

		// standard offset
		r_refdef.viewheight_test = 10;
	}

	// set up refresh view angles
	VectorCopy(cl.simangles, r_refdef.viewangles);
	V_CalcViewRoll();
	V_AddIdle();

	if (v_gunkick.value) {
		// add weapon kick offset
		AngleVectors(r_refdef.viewangles, forward, NULL, NULL);
		VectorMA(r_refdef.vieworg, cl.punchangle, forward, r_refdef.vieworg);

		// add weapon kick angle
		r_refdef.viewangles[PITCH] += cl.punchangle * 0.5;
	}

	if (view_message.flags & PF_DEAD && (cl.stats[STAT_HEALTH] <= 0)) {
		r_refdef.viewangles[ROLL] = 80;	// dead view angle
	}

	//VULT CAMERAS
	CameraUpdate(view_message.flags & PF_DEAD);

	// meag: really viewheight shouldn't be here, but it was incorrectly passed for years instead of bob,
	//       and so without it the gun is rendered too far forward if e.g. viewheight -6
	V_AddViewWeapon(bob + height_adjustment);
}

void DropPunchAngle (void) {
	if (cl.ideal_punchangle < cl.punchangle) {
		if (cl.ideal_punchangle == -2)	// small kick
			cl.punchangle -= 20 * cls.frametime;
		else							// big kick
			cl.punchangle -= 40 * cls.frametime;

		if (cl.punchangle <= cl.ideal_punchangle) {
			cl.punchangle = cl.ideal_punchangle;
			cl.ideal_punchangle = 0;
		}
	}else {
		cl.punchangle += 20 * cls.frametime;
		if (cl.punchangle > 0)
			cl.punchangle = 0;
	}
}

//The player's clipping box goes from (-16 -16 -24) to (16 16 32) from
//the entity origin, so any view position inside that will be valid
extern vrect_t scr_vrect;

qbool V_PreRenderView(void)
{
	char *p;

	cl.simangles[ROLL] = 0;	// FIXME @@@ 

	if (cls.state != ca_active) {
		V_CalcBlend();
	}
	else {
		view_frame = &cl.frames[cl.validsequence & UPDATE_MASK];
		if (!cls.nqdemoplayback) {
			view_message = view_frame->playerstate[cl.viewplayernum];
		}

		DropPunchAngle();
		if (cl.intermission) {
			// intermission / finale rendering
			V_CalcIntermissionRefdef();
		}
		else {
			V_CalcRefdef();
		}

		MVD_PowerupCam_Frame();

		R_PushDlights();

		r_refdef2.time = cl.time;
		r_refdef2.sin_time = sin(r_refdef2.time);
		r_refdef2.cos_time = cos(r_refdef2.time);

		// scroll parameters for powerup shells
		r_refdef2.powerup_scroll_params[0] = cos(cl.time * 1.5);
		r_refdef2.powerup_scroll_params[1] = sin(cl.time * 1.1);
		r_refdef2.powerup_scroll_params[2] = cos(cl.time * -0.5);
		r_refdef2.powerup_scroll_params[3] = sin(cl.time * -0.5);

		// restrictions
		r_refdef2.allow_cheats = cls.demoplayback || (Info_ValueForKey(cl.serverinfo, "*cheats")[0] && com_serveractive);
		if (cls.demoplayback || cl.spectator) {
			r_refdef2.allow_lumas = true;
			r_refdef2.max_fbskins = 1;
		}
		else {
			r_refdef2.allow_lumas = !strcmp(Info_ValueForKey(cl.serverinfo, "24bit_fbs"), "0") ? false : true;
			r_refdef2.max_fbskins = *(p = Info_ValueForKey(cl.serverinfo, "fbskins")) ? bound(0, Q_atof(p), 1) : (cl.teamfortress ? 0 : 1);
		}

		// Only allow alpha water if the server allows it, or they are spectator and have novis enabled
		{
			extern cvar_t r_novis;

			r_refdef2.max_watervis = *(p = Info_ValueForKey(cl.serverinfo, "watervis")) ? bound(0, Q_atof(p), 1) : 0;
			if ((cls.demoplayback || cl.spectator) && (r_novis.integer || r_refdef2.max_watervis > 0)) {
				// ignore server limit
				r_refdef2.max_watervis = 1;
			}
			r_refdef2.wateralpha = R_WaterAlpha();  // relies on r_refdef2.max_watervis
		}

		// time-savers
		{
			extern cvar_t r_drawflat_mode, r_drawflat, r_fastturb, gl_caustics;
			extern texture_ref underwatertexture;

			r_refdef2.drawFlatFloors = r_drawflat_mode.integer == 0 && (r_drawflat.integer == 2 || r_drawflat.integer == 1);
			r_refdef2.drawFlatWalls = r_drawflat_mode.integer == 0 && (r_drawflat.integer == 3 || r_drawflat.integer == 1);
			r_refdef2.solidTexTurb = (!r_fastturb.integer && r_refdef2.wateralpha == 1);

			r_refdef2.drawCaustics = (R_TextureReferenceIsValid(underwatertexture) && gl_caustics.integer);
			r_refdef2.drawWorldOutlines = R_DrawWorldOutlines();
			r_refdef2.distanceScale = tan(r_refdef.fov_x * (M_PI / 180) * 0.5f);
			VectorScale(vpn, 0.002 * r_refdef2.distanceScale, r_refdef2.outline_vpn);
			r_refdef2.outlineBase = 1 - DotProduct(r_origin, r_refdef2.outline_vpn);
		}
	}

	renderer.PreRenderView();

	return cls.state == ca_active;
}

//============================================================================

void V_Init (void) {
	Cmd_AddCommand ("v_cshift", V_cshift_f);	
	Cmd_AddCommand ("bf", V_BonusFlash_f);
	Cmd_AddCommand ("centerview", V_StartPitchDrift);
	Cmd_AddCommand ("force_centerview", Force_Centerview_f);

	Cvar_SetCurrentGroup(CVAR_GROUP_VIEW);
	Cvar_Register (&v_centermove);
	Cvar_Register (&v_centerspeed);
	Cvar_Register (&cl_rollspeed);
	Cvar_Register (&cl_rollangle);
	Cvar_Register (&cl_rollalpha);

	Cvar_Register (&v_idlescale);
	Cvar_Register (&v_iyaw_cycle);
	Cvar_Register (&v_iroll_cycle);
	Cvar_Register (&v_ipitch_cycle);
	Cvar_Register (&v_iyaw_level);
	Cvar_Register (&v_iroll_level);
	Cvar_Register (&v_ipitch_level);
	Cvar_Register (&r_nearclip);

	Cvar_SetCurrentGroup(CVAR_GROUP_CROSSHAIR);
	Cvar_Register(&crosshaircolor);
	Cvar_Register(&crosshair);
	Cvar_Register(&crosshairsize);
	Cvar_Register(&cl_crossx);
	Cvar_Register(&cl_crossy);

	Cvar_SetCurrentGroup(CVAR_GROUP_VIEWMODEL);
	Cvar_Register(&cl_bob);
	Cvar_Register(&cl_bobcycle);
	Cvar_Register(&cl_bobup);
	Cvar_Register(&cl_bobhead);

	Cvar_Register(&cl_drawgun);
	Cvar_Register(&cl_drawgun_invisible);
	Cvar_Register(&r_viewmodelsize);
	Cvar_Register(&r_viewmodeloffset);
	Cvar_Register(&r_viewpreselgun);
	Cvar_Register(&r_viewlastfired);

	Cvar_SetCurrentGroup(CVAR_GROUP_VIEW);
	Cvar_Register (&v_kicktime);
	Cvar_Register (&v_kickroll);
	Cvar_Register (&v_kickpitch);
	Cvar_Register (&v_gunkick);


	Cvar_Register (&v_viewheight);


	Cvar_SetCurrentGroup(CVAR_GROUP_BLEND);

	Cvar_Register (&v_bonusflash);
	Cvar_Register (&v_contentblend);
	Cvar_Register (&v_damagecshift);
	Cvar_Register (&v_quadcshift);
	Cvar_Register (&v_suitcshift);
	Cvar_Register (&v_ringcshift);
	Cvar_Register (&v_pentcshift);
	Cvar_Register (&cl_demoplay_flash); // from QW262

	Cvar_Register(&v_dlightcshift);
	Cvar_Register(&v_dlightcolor);
	Cvar_Register(&v_dlightcshiftpercent);
	Cvar_Register(&gl_cshiftpercent);
	Cvar_Register(&gl_hwblend);
	Cvar_Register(&vid_hwgamma_fps);

	Cvar_SetCurrentGroup(CVAR_GROUP_SCREEN);
	Cvar_Register(&v_gamma);
	Cvar_Register(&v_contrast);

	// we do not need this after host initialized
	if (!host_initialized) {
		int i;
		float def_gamma = 1.0f;
		extern float vid_gamma;

		if ((i = COM_CheckParm(cmdline_param_client_gamma)) != 0 && i + 1 < COM_Argc()) {
			def_gamma = Q_atof(COM_Argv(i + 1));
			def_gamma = bound(0.3, def_gamma, 3);
			Cvar_SetDefaultAndValue(&v_gamma, def_gamma, def_gamma);
			vid_gamma = def_gamma;
		}
		else {
			vid_gamma = 1.0;
		}

		v_gamma.modified = true;
	}
	Cvar_ResetCurrentGroup();
}
