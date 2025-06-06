/*
Copyright (C) 2011 azazello and ezQuake team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// hud_teaminfo.c

#include "quakedef.h"
#include "hud.h"
#include "teamplay.h"
#include "hud_common.h"
#include "vx_stuff.h"
#include "utils.h"
// Get rid of these once we remove matrix scaling from cl_screen.c version...
#include "gl_model.h"
#include "fonts.h"

#define FONT_WIDTH 8

static void Update_TeamInfo(void);
mpic_t* SCR_GetWeaponIconByFlag(int flag);

void OnChange_scr_mega_color (cvar_t *var, char *string, qbool *cancel);

static cvar_t scr_shownick_order           = { "scr_shownick_order", "%p%n %a/%H %w" };
static cvar_t scr_shownick_frame_color     = { "scr_shownick_frame_color", "10 0 0 120", CVAR_COLOR };
static cvar_t scr_shownick_scale           = { "scr_shownick_scale",		"1" };
static cvar_t scr_shownick_y               = { "scr_shownick_y",			"0" };
static cvar_t scr_shownick_x               = { "scr_shownick_x",			"0" };
static cvar_t scr_shownick_name_width      = { "scr_shownick_name_width",	"6" };
static cvar_t scr_shownick_time            = { "scr_shownick_time",		"0.8" };
static cvar_t scr_shownick_proportional    = { "scr_shownick_proportional", "0" };
static cvar_t scr_shownick_show_ammo	   = { "scr_shownick_show_ammo", "0" };

static cvar_t scr_teaminfo_order           = { "scr_teaminfo_order", "%p%n $x10%l$x11 %a/%H %w", CVAR_NONE };
static cvar_t scr_teaminfo_align_right     = { "scr_teaminfo_align_right", "1" };
static cvar_t scr_teaminfo_frame_color     = { "scr_teaminfo_frame_color", "10 0 0 120", CVAR_COLOR };
static cvar_t scr_teaminfo_scale           = { "scr_teaminfo_scale",       "1" };
static cvar_t scr_teaminfo_y               = { "scr_teaminfo_y",           "0" };
static cvar_t scr_teaminfo_x               = { "scr_teaminfo_x",           "0" };
static cvar_t scr_teaminfo_loc_width       = { "scr_teaminfo_loc_width",   "5" };
static cvar_t scr_teaminfo_name_width      = { "scr_teaminfo_name_width",  "6" };
static cvar_t scr_teaminfo_low_health      = { "scr_teaminfo_low_health",  "25" };
static cvar_t scr_teaminfo_armor_style     = { "scr_teaminfo_armor_style", "3" };
static cvar_t scr_teaminfo_powerup_style   = { "scr_teaminfo_powerup_style", "1" };
static cvar_t scr_teaminfo_flag_style      = { "scr_teaminfo_flag_style",  "1" };
static cvar_t scr_teaminfo_weapon_style    = { "scr_teaminfo_weapon_style","1" };
static cvar_t scr_teaminfo_show_ammo	   = { "scr_teaminfo_show_ammo","0" };
static cvar_t scr_teaminfo_show_countdown  = { "scr_teaminfo_show_countdown","1" };
static cvar_t scr_teaminfo_show_enemies    = { "scr_teaminfo_show_enemies","0" };
static cvar_t scr_teaminfo_mega_color = { "scr_teaminfo_mega_color", "9cf", CVAR_NONE, OnChange_scr_mega_color };
static cvar_t scr_teaminfo_show_self       = { "scr_teaminfo_show_self",   "2" };
static cvar_t scr_teaminfo_proportional    = { "scr_teaminfo_proportional", "0"};
cvar_t scr_teaminfo                        = { "scr_teaminfo",             "1" };   // non-static for menu

static int SCR_HudDrawTeamInfoPlayer(ti_player_t *ti_cl, float x, int y, int maxname, int maxloc, qbool width_only, float scale, const char* layout, int weapon_style, int show_ammo, int show_countdown, int armor_style, int powerup_style, int flag_style, int low_health, qbool proportional);

static int HUD_CompareTeamInfoSlots(const void* lhs_, const void* rhs_)
{
	int lhs = *(const int*)lhs_;
	int rhs = *(const int*)rhs_;
	int lhs_pos = -1;
	int rhs_pos = -1;
	int i;

	for (i = 0; i < n_players; ++i) {
		if (sorted_players[i].playernum == lhs) {
			lhs_pos = i;
		}
		if (sorted_players[i].playernum == rhs) {
			rhs_pos = i;
		}
	}

	return lhs_pos - rhs_pos;
}

void OnChange_scr_mega_color (cvar_t *var, char *string, qbool *cancel)
{
	if (!Utils_RegExpMatch("^[0-9a-fA-F]{3}$", string))
		*cancel = true;
}

void SCR_HUD_DrawTeamInfo(hud_t *hud)
{
	int x, y, _y, width, height;
	int i, j, k, slots[MAX_CLIENTS], slots_num, maxname, maxloc;
	char tmp[1024], *nick;
	float header_spacing;

	// Used for hud_teaminfo, data is collected in screen.c / scr_teaminfo
	extern ti_player_t ti_clients[MAX_CLIENTS];

	extern qbool hud_editor;

	static cvar_t
		*hud_teaminfo_weapon_style = NULL,
		*hud_teaminfo_align_right,
		*hud_teaminfo_loc_width,
		*hud_teaminfo_name_width,
		*hud_teaminfo_show_ammo,
		*hud_teaminfo_show_countdown,
		*hud_teaminfo_show_enemies,
		*hud_teaminfo_show_self,
		*hud_teaminfo_show_headers,
		*hud_teaminfo_scale,
		*hud_teaminfo_armor_style,
		*hud_teaminfo_powerup_style,
		*hud_teaminfo_flag_style,
		*hud_teaminfo_low_health,
		*hud_teaminfo_layout,
		*hud_teaminfo_proportional,
		*hud_teaminfo_header_spacing;

	if (hud_teaminfo_weapon_style == NULL) {
		// first time
		hud_teaminfo_weapon_style = HUD_FindVar(hud, "weapon_style");
		hud_teaminfo_align_right = HUD_FindVar(hud, "align_right");
		hud_teaminfo_loc_width = HUD_FindVar(hud, "loc_width");
		hud_teaminfo_name_width = HUD_FindVar(hud, "name_width");
		hud_teaminfo_show_ammo = HUD_FindVar(hud, "show_ammo");
		hud_teaminfo_show_enemies = HUD_FindVar(hud, "show_enemies");
		hud_teaminfo_show_self = HUD_FindVar(hud, "show_self");
		hud_teaminfo_show_headers = HUD_FindVar(hud, "show_headers");
		hud_teaminfo_show_countdown = HUD_FindVar(hud, "show_countdown");
		hud_teaminfo_scale = HUD_FindVar(hud, "scale");
		hud_teaminfo_armor_style = HUD_FindVar(hud, "armor_style");
		hud_teaminfo_powerup_style = HUD_FindVar(hud, "powerup_style");
		hud_teaminfo_flag_style = HUD_FindVar(hud, "flag_style");
		hud_teaminfo_low_health = HUD_FindVar(hud, "low_health");
		hud_teaminfo_layout = HUD_FindVar(hud, "layout");
		hud_teaminfo_proportional = HUD_FindVar(hud, "proportional");
		hud_teaminfo_header_spacing = HUD_FindVar(hud, "header_spacing");
	}

	// Don't update hud item unless first view is beeing displayed
	if (CL_MultiviewCurrentView() != 1 && CL_MultiviewCurrentView() != 0) {
		return;
	}

	if (cls.mvdplayback) {
		Update_TeamInfo();
	}

	// fill data we require to draw teaminfo
	for (maxloc = maxname = slots_num = i = 0; i < MAX_CLIENTS; i++) {
		if (!cl.players[i].name[0] || cl.players[i].spectator || !ti_clients[i].time || ti_clients[i].time + 5 < r_refdef2.time) {
			continue;
		}

		// do not show enemy players unless it's MVD and user wishes to show them
		if (VX_TrackerIsEnemy(i) && (!cls.mvdplayback || !hud_teaminfo_show_enemies->integer)) {
			continue;
		}

		// do not show tracked player to spectator
		if ((cl.spectator && Cam_TrackNum() == i) && hud_teaminfo_show_self->integer == 0) {
			continue;
		}

		// dynamically guess max length of name/location
		nick = (ti_clients[i].nick[0] ? ti_clients[i].nick : cl.players[i].name); // we use nick or name
		maxname = max(maxname, strlen(TP_ParseFunChars(nick, false)));

		strlcpy(tmp, TP_LocationName(ti_clients[i].org), sizeof(tmp));
		maxloc = max(maxloc, strlen(TP_ParseFunChars(tmp, false)));

		slots[slots_num++] = i;
	}

	qsort(slots, slots_num, sizeof(slots[0]), HUD_CompareTeamInfoSlots);

	// well, better use fixed loc length
	maxloc = bound(0, hud_teaminfo_loc_width->integer, 100);
	// limit name length
	maxname = bound(0, maxname, hud_teaminfo_name_width->integer);

	header_spacing = max(0, hud_teaminfo_header_spacing->value);

	// this doesn't draw anything, just calculate width
	width = SCR_HudDrawTeamInfoPlayer(&ti_clients[0], 0, 0, maxname, maxloc, true, hud_teaminfo_scale->value, hud_teaminfo_layout->string, hud_teaminfo_weapon_style->integer, hud_teaminfo_show_ammo->integer, hud_teaminfo_show_countdown->integer, hud_teaminfo_armor_style->integer, hud_teaminfo_powerup_style->integer, hud_teaminfo_flag_style->integer, hud_teaminfo_low_health->integer, hud_teaminfo_proportional->integer);
	height = FONTWIDTH * hud_teaminfo_scale->value * (hud_teaminfo_show_enemies->integer && hud_teaminfo_show_headers->integer ? slots_num + max(2 * n_teams - 1, 0) * header_spacing : slots_num);

	if (hud_editor) {
		HUD_PrepareDraw(hud, width, FONTWIDTH, &x, &y);
	}

	if (!slots_num) {
		return;
	}

	if (!cl.teamplay) {
		// non teamplay mode
		return;
	}

	if (!HUD_PrepareDraw(hud, width, height, &x, &y)) {
		return;
	}

	_y = y;
	x = (hud_teaminfo_align_right->value ? x - (width * (FONTWIDTH * hud_teaminfo_scale->value)) : x);

	// If multiple teams are displayed then sort the display and print team header on overlay
	k = 0;
	if (hud_teaminfo_show_enemies->integer) {
		while (sorted_teams[k].name) {
			// hmx : different name/scores alignment options are possible in the header
			// in which case, make sure to differentiate name width vs teaminfo width
			// i.e int name_width = Draw_SString()
			if (hud_teaminfo_show_headers->integer) {
				if (k > 0) { // separator between teams
					_y += FONTWIDTH * hud_teaminfo_scale->value * header_spacing;
				}

				Draw_SString(x, _y, sorted_teams[k].name, hud_teaminfo_scale->value, hud_teaminfo_proportional->integer);
				snprintf(tmp, sizeof(tmp), "%s %4i", TP_ParseFunChars("$.", false), sorted_teams[k].frags);
				Draw_SStringAligned(x, _y, tmp, hud_teaminfo_scale->value, 1.0f, hud_teaminfo_proportional->integer, text_align_right, x + width);
				_y += FONTWIDTH * hud_teaminfo_scale->value;
			}
			for (j = 0; j < slots_num; j++) {
				i = slots[j];
				if (!strcmp(cl.players[i].team, sorted_teams[k].name)) {
					SCR_HudDrawTeamInfoPlayer(&ti_clients[i], x, _y, maxname, maxloc, false, hud_teaminfo_scale->value, hud_teaminfo_layout->string, hud_teaminfo_weapon_style->integer, hud_teaminfo_show_ammo->integer, hud_teaminfo_show_countdown->integer, hud_teaminfo_armor_style->integer, hud_teaminfo_powerup_style->integer, hud_teaminfo_flag_style->integer, hud_teaminfo_low_health->integer, hud_teaminfo_proportional->integer);
					_y += FONTWIDTH * hud_teaminfo_scale->value;
				}
			}
			k++;
		}
	}
	else {
		for (j = 0; j < slots_num; j++) {
			i = slots[j];
			SCR_HudDrawTeamInfoPlayer(&ti_clients[i], x, _y, maxname, maxloc, false, hud_teaminfo_scale->value, hud_teaminfo_layout->string, hud_teaminfo_weapon_style->integer, hud_teaminfo_show_ammo->integer, hud_teaminfo_show_countdown->integer, hud_teaminfo_armor_style->integer, hud_teaminfo_powerup_style->integer, hud_teaminfo_flag_style->integer, hud_teaminfo_low_health->integer, hud_teaminfo_proportional->integer);
			_y += FONTWIDTH * hud_teaminfo_scale->value;
		}
	}
}

qbool Has_Both_RL_and_LG(int flags)
{
	return (flags & IT_ROCKET_LAUNCHER) && (flags & IT_LIGHTNING);
}

static int SCR_HudDrawTeamInfoPlayer(ti_player_t *ti_cl, float x, int y, int maxname, int maxloc, qbool width_only, float scale, const char* layout, int weapon_style, int show_ammo, int show_countdown, int armor_style, int powerup_style, int flag_style, int low_health, qbool proportional)
{
	extern cvar_t tp_name_rlg;
	char *s, *loc, tmp[1024], tmp2[MAX_MACRO_STRING], *aclr, *txtclr;
	float x_in = x; // save x
	int i, a;
	qbool isDeadCA, isRespawning;
	mpic_t *pic;
	float width;
	float font_width = scale * FONT_WIDTH;
	float alpha;

	extern mpic_t *sb_face_invis, *sb_face_quad, *sb_face_invuln;
	extern mpic_t *sb_armor[3];
	extern mpic_t *sb_items[32];

	if (!ti_cl) {
		return 0;
	}

	txtclr = "&cfff";
	isDeadCA = ti_cl->isdead;
	isRespawning = isDeadCA && ti_cl->timetospawn > 0 && ti_cl->timetospawn < 999;
	
	if (isDeadCA) {
		alpha = 0.25;
	}
	else {
		alpha = 1.0;
	}

	i = ti_cl->client;
	if (i < 0 || i >= MAX_CLIENTS) {
		Com_DPrintf("SCR_Draw_TeamInfoPlayer: wrong client %d\n", i);
		return 0;
	}

	// this limit len of string because TP_ParseFunChars() do not check overflow
	strlcpy(tmp2, layout, sizeof(tmp2));
	strlcpy(tmp2, TP_ParseFunChars(tmp2, false), sizeof(tmp2));
	s = tmp2;

	switch (BestWeaponFromStatItems(ti_cl->items)){
		case IT_LIGHTNING:
			a = ti_cl->cells;
			break;
		case IT_ROCKET_LAUNCHER:
		case IT_GRENADE_LAUNCHER:
			a = ti_cl->rockets;
			break;
		case IT_SUPER_NAILGUN:
		case IT_NAILGUN:
			a = ti_cl->nails;
			break;
		default:
			a = ti_cl->shells;
			break;
	}

	//
	// parse/draw string like this "%n %h:%a %l %p %w"
	//
	for (; *s; s++) {
		switch ((int)s[0]) {
			case '%':
				s++; // advance

				switch ((int)s[0]) {
					case 'n': // draw name
						width = maxname * font_width;
						if (!width_only) {
							char *nick = TP_ParseFunChars(ti_cl->nick[0] ? ti_cl->nick : cl.players[i].shortname, false);
							snprintf(tmp, sizeof(tmp), "%s%s", txtclr, nick);
							Draw_SStringAligned(x, y, tmp, scale, alpha, proportional, text_align_right, x + width);
						}
						x += width;
						break;
					case 'w': // draw "best" weapon icon/name
					case 'W': // draw "best" weapon icon/name
						switch (weapon_style) {
							case 1:
								if (!width_only) {
									char *weap_str;
									if (Has_Both_RL_and_LG(ti_cl->items)) {
										weap_str = tp_name_rlg.string;
									} 
									else {
										weap_str = TP_ItemName(BestWeaponFromStatItems(ti_cl->items));
									}
									char weap_white_stripped[32];
									Util_SkipChars(weap_str, "{}", weap_white_stripped, 32);
									snprintf(tmp, sizeof(tmp), "%s%s", txtclr, isDeadCA ? "--" : weap_white_stripped);
									Draw_SStringAligned(x, y, tmp, scale, alpha, proportional, ((s[0] == 'W' || show_ammo) ? text_align_right : text_align_left), x + 3 * font_width);
								}
								x += 3 * font_width;
								break;
							default: // draw image by default
								if (!width_only) {
									if (!isDeadCA && (pic = SCR_GetWeaponIconByFlag(BestWeaponFromStatItems(ti_cl->items)))) {
										Draw_SAlphaPic(x, y, pic, alpha, 0.5 * scale);
									}
								}
								x += 2 * font_width;
								break;
						}
						if (show_ammo) {
							if (!width_only) {
								if (isDeadCA){
									snprintf(tmp, sizeof(tmp), "%s", txtclr);
								} 
								else {
									snprintf(tmp, sizeof(tmp), "%s:%d", txtclr, a);
								}
								
								Draw_SStringAligned(x, y, tmp, scale, alpha, proportional, text_align_left, x + 4 * font_width);
								
							}
							x += 4 * font_width;
						}
						break;
					case 'k': // draw ammo
					case 'K': // draw ammo
						if (!width_only) {
							snprintf(tmp, sizeof(tmp), "%s%d", txtclr, a);
							Draw_SStringAligned(x, y, tmp, scale, alpha, proportional, (s[0] == 'C' ? text_align_right : text_align_left), x + 3 * font_width);
						}
						x += 3 * font_width;
						break;
					case 'r': // draw respawn time
					case 'R': // draw respawn time
						if (!width_only) {
							if (isRespawning)
							{
								snprintf(tmp, sizeof(tmp), "&cfa0%d", ti_cl->timetospawn);
							}
							else
							{
								snprintf(tmp, sizeof(tmp), "%s", " ");
							}
							Draw_SStringAligned(x, y, tmp, scale * 0.75, 1.0, proportional, (s[0] == 'r' ? text_align_right : text_align_left), x + 2 * font_width);
						}
						x += 2 * font_width;
						break;
					case 'h': // draw health, padding with space on left side
					case 'H': // draw health, padding with space on right side
						if (!width_only) {
							if (isDeadCA){
								snprintf(tmp, sizeof(tmp), "%s--", txtclr); // print dashes if dead
							}
							else {
								snprintf(tmp, sizeof(tmp), "%s%s%d", "&c", (ti_cl->health < low_health ? "f00" : (ti_cl->health > 100 ? scr_teaminfo_mega_color.string : "fff")), ti_cl->health);
							}
							Draw_SStringAligned(x, y, tmp, scale, alpha, proportional, (s[0] == 'h' ? text_align_right : text_align_left), x + 3 * font_width);
						}
						x += 3 * font_width;
						break;

					case 'f': // draw frags, space on left side
					case 'F': // draw frags, space on right side
						if (!width_only) {
							snprintf(tmp, sizeof(tmp), (s[0] == 'f' ? "%s%3d" : "%s%-3d"), txtclr, cl.players[i].frags);
							Draw_SStringAligned(x, y, tmp, scale, alpha, proportional, (s[0] == 'f' ? text_align_right : text_align_left), x + 3 * font_width);
						}
						x += 3 * FONTWIDTH * scale;
						break;

					case 'a': // draw armor, padded with space on left side
					case 'A': // draw armor, padded with space on right side
						//
						// different styles of armor
						//
						aclr = txtclr;
						switch (armor_style) {
							case 1: // image prefixed armor value
								if (!width_only) {
									if (ti_cl->items & IT_ARMOR3)
										Draw_SAlphaPic(x, y, sb_armor[2], alpha, 1.0 / 3 * scale);
									else if (ti_cl->items & IT_ARMOR2)
										Draw_SAlphaPic(x, y, sb_armor[1], alpha, 1.0 / 3 * scale);
									else if (ti_cl->items & IT_ARMOR1)
										Draw_SAlphaPic(x, y, sb_armor[0], alpha, 1.0 / 3 * scale);
								}
								x += font_width;
								break;
							case 2: // colored background of armor value
								/*
									if (!width_only) {
										byte col[4] = {255, 255, 255, 0};

										if (ti_cl->items & IT_ARMOR3) {
											col[0] = 255; col[1] =   0; col[2] =   0; col[3] = 255;
										}
										else if (ti_cl->items & IT_ARMOR2) {
											col[0] = 255; col[1] = 255; col[2] =   0; col[3] = 255;
										}
										else if (ti_cl->items & IT_ARMOR1) {
											col[0] =   0; col[1] = 255; col[2] =   0; col[3] = 255;
										}
									}
								*/
								break;
							case 3: // colored armor value
								if (!width_only) {
									if (ti_cl->items & IT_ARMOR3) {
										aclr = "&cf00";
									}
									else if (ti_cl->items & IT_ARMOR2) {
										aclr = "&cff0";
									}
									else if (ti_cl->items & IT_ARMOR1) {
										aclr = "&c0f0";
									}
								}
								break;
							case 4: // armor value prefixed with letter
								if (!width_only) {
									if (ti_cl->items & IT_ARMOR3)
										Draw_SStringAlpha(x, y, "r", scale, alpha, proportional);
									else if (ti_cl->items & IT_ARMOR2)
										Draw_SStringAlpha(x, y, "y", scale, alpha, proportional);
									else if (ti_cl->items & IT_ARMOR1)
										Draw_SStringAlpha(x, y, "g", scale, alpha, proportional);
								}
								x += font_width;
								break;
						}

						if (!width_only) {
							// value drawn no matter which style
							if (isDeadCA){
								snprintf(tmp, sizeof(tmp), "%s--", txtclr); // print dashes if dead
							}
							else {
								snprintf(tmp, sizeof(tmp), "%s%d", aclr, ti_cl->armor);
							}
							Draw_SStringAligned(x, y, tmp, scale, alpha, proportional, s[0] == 'A' ? text_align_left : text_align_right, x + 3 * font_width);
						}
						x += 3 * font_width;
						break;

					case 'l': // draw location
						width = maxloc * font_width;
						if (!width_only) {
							loc = TP_LocationName(ti_cl->org);
							if (!loc[0]) {
								loc = "unknown";
							}
							loc = TP_ParseFunChars(loc, false);
							if (isDeadCA){
								snprintf(tmp, sizeof(tmp), "%sdead", txtclr);
							}
							else {
								snprintf(tmp, sizeof(tmp), "%s%s", txtclr, loc);
							}
							Draw_SStringAligned(x, y, tmp, scale, alpha, proportional, text_align_right, x + width);
						}
						x += width;
						break;

					case 'p': // draw powerups
						if (show_countdown && check_ktx_ca_wo())
						{
							if (!width_only) {
								if (isRespawning)
								{
									snprintf(tmp, sizeof(tmp), "&cfa0%d ", ti_cl->timetospawn);
								}
								else
								{
									snprintf(tmp, sizeof(tmp), "%s", " ");
								}
								Draw_SStringAligned(x, y, tmp, scale * 0.75, 1.0, proportional, text_align_right, x + 3 * font_width);
							}
							x += 3 * font_width;
							break;
						}
						else {
							switch (powerup_style) {
								case 1: // quad/pent/ring image
									if (!width_only) {
										if (ti_cl->items & IT_QUAD) {
											Draw_SPic(x, y, sb_items[5], scale / 2);
										}
										x += font_width;
										if (ti_cl->items & IT_INVULNERABILITY) {
											Draw_SPic(x, y, sb_items[3], scale / 2);
										}
										x += font_width;
										if (ti_cl->items & IT_INVISIBILITY) {
											Draw_SPic(x, y, sb_items[2], scale / 2);
										}
										x += font_width;
									}
									else { 
										x += 3 * font_width;
									}
									break;

								case 2: // player powerup face
									if (!width_only) {
										if (sb_face_quad && (ti_cl->items & IT_QUAD)) {
											Draw_SPic(x, y, sb_face_quad, scale / 3);
										}
										x += font_width;
										if (sb_face_invuln && (ti_cl->items & IT_INVULNERABILITY)) {
											Draw_SPic(x, y, sb_face_invuln, scale / 3);
										}
										x += font_width;
										if (sb_face_invis && (ti_cl->items & IT_INVISIBILITY)) {
											Draw_SPic(x, y, sb_face_invis, scale / 3);
										}
										x += font_width;
									}
									else {
										x += 3 * font_width;
									}
									break;

								case 3: // colored font (QPR)
									if (!width_only) {
										if (ti_cl->items & IT_QUAD) {
											Draw_SString(x, y, "&c03fQ", scale, proportional);
										}
										x += font_width;
										if (ti_cl->items & IT_INVULNERABILITY) {
											Draw_SString(x, y, "&cf00P", scale, proportional);
										}
										x += font_width;
										if (ti_cl->items & IT_INVISIBILITY) {
											Draw_SString(x, y, "&cff0R", scale, proportional);
										}
										x += font_width;
									}
									else {
										x += 3 * font_width;
									}
									break;
							}
						}
						break;

					case 't':
						if (!width_only) {
							sprintf(tmp, "%s%i", txtclr, Player_GetTrackId(cl.players[ti_cl->client].userid));
							Draw_SStringAlpha(x, y, tmp, scale, alpha, proportional);
						}
						x += (Player_GetTrackId(cl.players[ti_cl->client].userid) >= 10 ? 2 : 1) * font_width;
						break;

					case 'c':
						switch (flag_style) {
							case 1:
								if (!width_only) {
									if (ti_cl->items & IT_KEY1) {
										Draw_SPic(x, y, sb_items[0], scale / 2);
									} else if (ti_cl->items & IT_KEY2) {
										Draw_SPic(x, y, sb_items[1], scale / 2);
									}
								}
								break;
							case 2:
								if (!width_only) {
									if (ti_cl->items & IT_KEY1) {
										Draw_SString(x, y, "&cf00B", scale, proportional);
									} else if (ti_cl->items & IT_KEY2) {
										Draw_SString(x, y, "&c00fR", scale, proportional);
									}
								}
								break;
						}
						x += font_width;
						break;

					case '%': // wow, %% result in one %, how smart
						if (!width_only) {
							snprintf(tmp, sizeof(tmp), "%s%s", txtclr, "%");
							Draw_SStringAlpha(x, y, tmp, scale, alpha, proportional);
						}
						x += font_width;
						break;

					default: // print %x - that mean sequence unknown
						if (!width_only) {
							snprintf(tmp, sizeof(tmp), "%%%c", s[0]);
							Draw_SStringAlpha(x, y, tmp, scale, alpha, proportional);
						}
						x += (s[0] ? 2 : 1) * font_width;
						break;
				}

				break;

			default: // print x
				if (!width_only) {
					snprintf(tmp, sizeof(tmp), "%s%c", txtclr, s[0]);
					if (s[0] != ' ') {
						// inhuman smart optimization, do not print space!
						Draw_SStringAlpha(x, y, tmp, scale, alpha, proportional);
					}
				}
				x += font_width;
				break;
		}
	}

	return (x - x_in); // return width
}


// ORIGINAL teaminfo (cl_screen.c)

// scr_teaminfo 
// Variable ti_clients and related functions also used by hud_teaminfo in hud_common.c
ti_player_t ti_clients[MAX_CLIENTS];

void SCR_ClearTeamInfo(void)
{
	memset(ti_clients, 0, sizeof(ti_clients));
}

void SCR_Draw_TeamInfo(void)
{
	int x, y, w, h;
	int i, j, slots[MAX_CLIENTS], slots_num, maxname, maxloc;
	char tmp[1024], *nick;

	float	scale = bound(0.1, scr_teaminfo_scale.value, 10);

	if (!cl.teamplay || !scr_teaminfo.integer) {
		// non teamplay mode
		return;
	}

	if (cls.mvdplayback) {
		Update_TeamInfo();
	}

	// fill data we require to draw teaminfo
	for (maxloc = maxname = slots_num = i = 0; i < MAX_CLIENTS; i++) {
		if (!cl.players[i].name[0] || cl.players[i].spectator
			|| !ti_clients[i].time || ti_clients[i].time + TI_TIMEOUT < r_refdef2.time
			)
			continue;

		// do not show enemy players unless it's MVD and user wishes to show them
		if (VX_TrackerIsEnemy(i) && (!cls.mvdplayback || !scr_teaminfo_show_enemies.integer))
			continue;

		// do not show tracked player to spectator
		if ((cl.spectator && Cam_TrackNum() == i) && !TEAMINFO_SHOWSELF())
			continue;

		// dynamically guess max length of name/location
		nick = (ti_clients[i].nick[0] ? ti_clients[i].nick : cl.players[i].name); // we use nick or name
		maxname = max(maxname, strlen(TP_ParseFunChars(nick, false)));

		strlcpy(tmp, TP_LocationName(ti_clients[i].org), sizeof(tmp));
		maxloc = max(maxloc, strlen(TP_ParseFunChars(tmp, false)));

		slots[slots_num++] = i;
	}

	// well, better use fixed loc length
	maxloc = bound(0, scr_teaminfo_loc_width.integer, 100);
	// limit name length
	maxname = bound(0, maxname, scr_teaminfo_name_width.integer);

	if (!slots_num) {
		return;
	}

	y = vid.height * 0.6 + scr_teaminfo_y.value;

	// this does't draw anything, just calculate width
	w = SCR_HudDrawTeamInfoPlayer(&ti_clients[0], 0, 0, maxname, maxloc, true, scr_teaminfo_scale.value, scr_teaminfo_order.string, scr_teaminfo_weapon_style.integer, scr_teaminfo_show_ammo.integer, scr_teaminfo_show_countdown.integer, scr_teaminfo_armor_style.integer, scr_teaminfo_powerup_style.integer, scr_teaminfo_flag_style.integer, scr_teaminfo_low_health.integer, scr_teaminfo_proportional.integer);
	h = slots_num * scale;

	for (j = 0; j < slots_num; j++) {
		i = slots[j];

		x = (scr_teaminfo_align_right.value ? (vid.width - w) - FONTWIDTH : FONTWIDTH);
		x += scr_teaminfo_x.value;

		if (!j) { // draw frame
			byte	*col = scr_teaminfo_frame_color.color;

			Draw_AlphaRectangleRGB(x, y, w, h * FONTWIDTH, 0, true, RGBAVECT_TO_COLOR(col));
		}

		SCR_HudDrawTeamInfoPlayer(&ti_clients[i], x, y, maxname, maxloc, false, scr_teaminfo_scale.value, scr_teaminfo_order.string, scr_teaminfo_weapon_style.integer, scr_teaminfo_show_ammo.integer, scr_teaminfo_show_countdown.integer, scr_teaminfo_armor_style.integer, scr_teaminfo_powerup_style.integer, scr_teaminfo_flag_style.integer, scr_teaminfo_low_health.integer, scr_teaminfo_proportional.integer);

		y += FONTWIDTH * scale;
	}
}

#define FLAGS_RUNES_MASK (IT_SIGIL1 | IT_SIGIL2 | IT_SIGIL3 | IT_SIGIL4 | IT_KEY1 | IT_KEY2)

static int Filter_FlagsAndRunes(int client, int stats)
{
	if (!Stats_IsFlagsParsed()) {
		return stats;
	}
	return (ti_clients[client].items & FLAGS_RUNES_MASK) | (stats & ~FLAGS_RUNES_MASK);
}

void Parse_TeamInfo(char *s)
{
	int		client;

	Cmd_TokenizeString(s);

	client = atoi(Cmd_Argv(1));

	if (client < 0 || client >= MAX_CLIENTS) {
		Com_DPrintf("Parse_TeamInfo: wrong client %d\n", client);
		return;
	}

	ti_clients[client].client = client; // no, its not stupid

	ti_clients[client].time = r_refdef2.time;

	ti_clients[client].org[0] = atoi(Cmd_Argv(2));
	ti_clients[client].org[1] = atoi(Cmd_Argv(3));
	ti_clients[client].org[2] = atoi(Cmd_Argv(4));
	ti_clients[client].health = atoi(Cmd_Argv(5));
	ti_clients[client].armor = atoi(Cmd_Argv(6));
	ti_clients[client].items = Filter_FlagsAndRunes(client, atoi(Cmd_Argv(7)));
	strlcpy(ti_clients[client].nick, Cmd_Argv(8), TEAMINFO_NICKLEN); // nick is optional
	ti_clients[client].shells = atoi(Cmd_Argv(9));
	ti_clients[client].nails = atoi(Cmd_Argv(10));
	ti_clients[client].rockets = atoi(Cmd_Argv(11));
	ti_clients[client].cells = atoi(Cmd_Argv(12));
}

void Parse_CAInfo(char *s)
{
	int		client;

	Cmd_TokenizeString(s);
	client = atoi(Cmd_Argv(1));

	if (client < 0 || client >= MAX_CLIENTS) {
		Com_DPrintf("Parse_CAInfo: wrong client %d\n", client);
		return;
	}

	if (!cls.mvdplayback) {
		ti_clients[client].client = client; // no, its not stupid
		ti_clients[client].time = r_refdef2.time;
		ti_clients[client].org[0] = atoi(Cmd_Argv(2));
		ti_clients[client].org[1] = atoi(Cmd_Argv(3));
		ti_clients[client].org[2] = atoi(Cmd_Argv(4));
		ti_clients[client].health = atoi(Cmd_Argv(5));
		ti_clients[client].armor = atoi(Cmd_Argv(6));
		ti_clients[client].items = atoi(Cmd_Argv(7));
		strlcpy(ti_clients[client].nick, Cmd_Argv(8), TEAMINFO_NICKLEN); // nick is optional
		ti_clients[client].shells = atoi(Cmd_Argv(9));
		ti_clients[client].nails = atoi(Cmd_Argv(10));
		ti_clients[client].rockets = atoi(Cmd_Argv(11));
		ti_clients[client].cells = atoi(Cmd_Argv(12));
	}
	
	ti_clients[client].camode = atoi(Cmd_Argv(13));
	ti_clients[client].isdead = atoi(Cmd_Argv(14));
	ti_clients[client].timetospawn = atoi(Cmd_Argv(15));
	ti_clients[client].round_kills = atoi(Cmd_Argv(16));
	ti_clients[client].round_deaths = atoi(Cmd_Argv(17));
}

void Update_FlagStatus(int player_num, char *team, qbool got_flag)
{
	int flag = IT_KEY1 | IT_KEY2;
	if (!strcmp(team, "blue")) {
		flag = IT_KEY2;
	} else if (!strcmp(team, "red")) {
		flag = IT_KEY1;
	}

	if (got_flag) {
		ti_clients[player_num].items |= flag;
	} else {
		ti_clients[player_num].items &= ~flag;
	}
}

static void Update_TeamInfo(void)
{
	int		i;
	int		*st;

	static double lastupdate = 0;

	if (!cls.mvdplayback) {
		return;
	}

	// don't update each frame - it's less disturbing
	if (cls.realtime - lastupdate < 1)
		return;

	lastupdate = cls.realtime;

	for (i = 0; i < MAX_CLIENTS; i++) {
		if (cl.players[i].spectator || !cl.players[i].name[0])
			continue;

		st = cl.players[i].stats;

		ti_clients[i].client = i; // no, its not stupid

		ti_clients[i].time = r_refdef2.time;

		VectorCopy(cl.frames[cl.parsecount & UPDATE_MASK].playerstate[i].origin, ti_clients[i].org);

		ti_clients[i].health = bound(0, st[STAT_HEALTH], 999);
		ti_clients[i].armor = bound(0, st[STAT_ARMOR], 999);
		ti_clients[i].items = Filter_FlagsAndRunes(i, st[STAT_ITEMS]);
		ti_clients[i].shells = bound(0, st[STAT_SHELLS], 999);
		ti_clients[i].nails = bound(0, st[STAT_NAILS], 999);
		ti_clients[i].rockets = bound(0, st[STAT_ROCKETS], 999);
		ti_clients[i].cells = bound(0, st[STAT_CELLS], 999);
		ti_clients[i].nick[0] = 0; // sad, we don't have nick, will use name
	}
}

void TeamInfo_HudInit(void)
{
	Cvar_SetCurrentGroup(CVAR_GROUP_SCREEN);
	Cvar_Register(&scr_shownick_order);
	Cvar_Register(&scr_shownick_frame_color);
	Cvar_Register(&scr_shownick_scale);
	Cvar_Register(&scr_shownick_y);
	Cvar_Register(&scr_shownick_x);
	Cvar_Register(&scr_shownick_name_width);
	Cvar_Register(&scr_shownick_time);
	Cvar_Register(&scr_shownick_proportional);
	Cvar_Register(&scr_shownick_show_ammo);

	Cvar_Register(&scr_teaminfo_order);
	Cvar_Register(&scr_teaminfo_align_right);
	Cvar_Register(&scr_teaminfo_frame_color);
	Cvar_Register(&scr_teaminfo_scale);
	Cvar_Register(&scr_teaminfo_y);
	Cvar_Register(&scr_teaminfo_x);
	Cvar_Register(&scr_teaminfo_loc_width);
	Cvar_Register(&scr_teaminfo_name_width);
	Cvar_Register(&scr_teaminfo_low_health);
	Cvar_Register(&scr_teaminfo_armor_style);
	Cvar_Register(&scr_teaminfo_powerup_style);
	Cvar_Register(&scr_teaminfo_flag_style);
	Cvar_Register(&scr_teaminfo_weapon_style);
	Cvar_Register(&scr_teaminfo_show_ammo);
	Cvar_Register(&scr_teaminfo_show_countdown);
	Cvar_Register(&scr_teaminfo_show_enemies);
	Cvar_Register(&scr_teaminfo_mega_color);
	Cvar_Register(&scr_teaminfo_show_self);
	Cvar_Register(&scr_teaminfo_proportional);
	Cvar_Register(&scr_teaminfo);

	HUD_Register(
		"teaminfo", NULL, "Show information about your team in short form.",
		0, ca_active, 0, SCR_HUD_DrawTeamInfo,
		"0", "", "right", "center", "0", "0", "0.2", "20 20 20", NULL,
		"layout", "%p%n $x10%l$x11 %a/%H %w",
		"align_right", "0",
		"loc_width", "5",
		"name_width", "6",
		"low_health", "25",
		"armor_style", "3",
		"weapon_style", "0",
		"show_ammo", "0",
		"show_countdown", "1",
		"show_enemies", "0",
		"show_self", "1",
		"show_headers", "1",
		"scale", "1",
		"powerup_style", "1",
		"flag_style", "1",
		"proportional", "0",
		"header_spacing", "1",
		NULL
	);
}



// SHOWNICK


/***************************** customizeable shownick *************************/

static ti_player_t shownick;

void SCR_ClearShownick(void)
{
	memset(&shownick, 0, sizeof(shownick));
}

void Parse_Shownick(char *s)
{
	int		client, version, arg;

	Cmd_TokenizeString(s);

	arg = 1;

	version = atoi(Cmd_Argv(arg++));

	switch (version) {
		case 1:
		{
			client = atoi(Cmd_Argv(arg++));

			if (client < 0 || client >= MAX_CLIENTS) {
				Com_DPrintf("Parse_Shownick: wrong client %d\n", client);
				return;
			}

			shownick.client = client;

			shownick.time = r_refdef2.time;

			shownick.org[0] = atoi(Cmd_Argv(arg++));
			shownick.org[1] = atoi(Cmd_Argv(arg++));
			shownick.org[2] = atoi(Cmd_Argv(arg++));
			shownick.health = atoi(Cmd_Argv(arg++));
			shownick.armor = atoi(Cmd_Argv(arg++));
			shownick.items = atoi(Cmd_Argv(arg++));
			strlcpy(shownick.nick, Cmd_Argv(arg++), TEAMINFO_NICKLEN); // nick is optional
			shownick.shells = atoi(Cmd_Argv(arg++));
			shownick.nails = atoi(Cmd_Argv(arg++));
			shownick.rockets = atoi(Cmd_Argv(arg++));
			shownick.cells = atoi(Cmd_Argv(arg++));

			return;
		}

		default:
			Com_DPrintf("Parse_Shownick: unsupported version %d\n", version);
			return;
	}
}

static void Update_Shownick(void)
{
	int		i;
	int		*st;

	if (!cls.mvdplayback) {
		return;
	}

	for (i = 0; i < MAX_CLIENTS; i++) {
		if (cl.players[i].spectator || !cl.players[i].name[0])
			continue;

		if (i != shownick.client)
			continue;
			
		st = cl.players[i].stats;

		shownick.client = i; // no, its not stupid

		VectorCopy(cl.frames[cl.parsecount & UPDATE_MASK].playerstate[i].origin, shownick.org);

		shownick.health = bound(0, st[STAT_HEALTH], 999);
		shownick.armor = bound(0, st[STAT_ARMOR], 999);
		shownick.items = st[STAT_ITEMS];
		shownick.shells = bound(0, st[STAT_SHELLS], 999);
		shownick.nails = bound(0, st[STAT_NAILS], 999);
		shownick.rockets = bound(0, st[STAT_ROCKETS], 999);
		shownick.cells = bound(0, st[STAT_CELLS], 999);
		shownick.nick[0] = 0; // sad, we don't have nick, will use name
	}
}

void SCR_Draw_ShowNick(void)
{
	qbool	scr_shownick_align_right = false;
	int		x, y, w, h;
	int		maxname, maxloc;
	byte	*col;
	float	scale = bound(0.1, scr_shownick_scale.value, 10);

	// check do we have something do draw
	if (!shownick.time || shownick.time + bound(0.1, scr_shownick_time.value, 3) < r_refdef2.time) {
		return;
	}

	if (cls.mvdplayback) {
		Update_Shownick();
	}

	// loc is unused
	maxloc = 0;

	// limit name length
	maxname = 999;
	maxname = bound(0, maxname, scr_shownick_name_width.integer);

	y = vid.height * 0.6 + scr_shownick_y.value;

	// this does't draw anything, just calculate width
	w = SCR_HudDrawTeamInfoPlayer(&shownick, 0, 0, maxname, maxloc, true, scale, scr_shownick_order.string, scr_teaminfo_weapon_style.integer, scr_shownick_show_ammo.integer, 0, scr_teaminfo_armor_style.integer, scr_teaminfo_powerup_style.integer, scr_teaminfo_flag_style.integer, scr_teaminfo_low_health.integer, scr_shownick_proportional.integer);
	h = FONTWIDTH * scale;

	x = (scr_shownick_align_right ? (vid.width - w) - FONTWIDTH : FONTWIDTH);
	x += scr_shownick_x.value;

	// draw frame
	col = scr_shownick_frame_color.color;

	Draw_AlphaRectangleRGB(x, y, w, h, 0, true, RGBAVECT_TO_COLOR(col));

	// draw shownick
	SCR_HudDrawTeamInfoPlayer(&shownick, x, y, maxname, maxloc, false, scale, scr_shownick_order.string, scr_teaminfo_weapon_style.integer, scr_shownick_show_ammo.integer, 0, scr_teaminfo_armor_style.integer, scr_teaminfo_powerup_style.integer, scr_teaminfo_flag_style.integer, scr_teaminfo_low_health.integer, scr_shownick_proportional.integer);
}
