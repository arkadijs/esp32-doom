idf_component_register(
  INCLUDE_DIRS include
  REQUIRES prboom-wad-tables
  SRCS
am_map.c
d_client.c
d_deh.c
d_items.c
d_main.c
doomdef.c
doomstat.c
dstrings.c
f_finale.c
f_wipe.c
g_game.c
gl_main.c
gl_texture.c
hu_lib.c
hu_stuff.c
info.c
lprintf.c
m_argv.c
m_bbox.c
m_cheat.c
m_menu.c
m_misc.c
m_random.c
md5.c
mmus2mid.c
p_ceilng.c
p_checksum.c
p_doors.c
p_enemy.c
p_floor.c
p_genlin.c
p_inter.c
p_lights.c
p_map.c
p_maputl.c
p_mobj.c
p_plats.c
p_pspr.c
p_saveg.c
p_setup.c
p_sight.c
p_spec.c
p_switch.c
p_telept.c
p_tick.c
p_user.c
r_bsp.c
r_data.c
r_demo.c
r_draw.c
r_filter.c
r_fps.c
r_main.c
r_patch.c
r_plane.c
r_segs.c
r_sky.c
r_things.c
s_sound.c
sounds.c
st_lib.c
st_stuff.c
tables.c
v_video.c
version.c
w_mmap.c
w_wad.c
wi_stuff.c
z_bmalloc.c
z_zone.c
)

target_compile_options(${COMPONENT_LIB} PRIVATE
  -Wno-error=char-subscripts -Wno-error=unused-value -Wno-error=unused-const-variable -Wno-error=unused-but-set-parameter
  -Wno-error=parentheses -Wno-error=int-to-pointer-cast -Wno-error=duplicate-decl-specifier -Wno-error=format-overflow
  -Wno-error=misleading-indentation -Wno-error=nonnull -Wno-error=address -Wno-error=sizeof-pointer-div
  -Wno-pointer-sign -Wno-unused-function -Wno-unused-value -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-but-set-parameter
  -Wno-maybe-uninitialized -Wno-missing-field-initializers -Wno-int-to-pointer-cast -Wno-misleading-indentation
  -Wno-char-subscripts -Wno-type-limits -Wno-format-overflow -Wno-implicit-fallthrough -Wno-duplicate-decl-specifier
  -Wno-nonnull -Wno-parentheses -Wno-address -Wno-sizeof-pointer-div -Wno-unused-but-set-variable -Wno-unused-but-set-variable
)
