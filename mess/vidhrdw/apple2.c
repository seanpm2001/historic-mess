/***************************************************************************

  vidhrdw/apple2.c

***************************************************************************/

#include "driver.h"
#include "vidhrdw/generic.h"
#include "includes/apple2.h"

/***************************************************************************/

static APPLE2_STRUCT old_a2;
static struct tilemap *text_tilemap;
static struct tilemap *dbltext_tilemap;
static struct tilemap *lores_tilemap;
static int text_videobase;
static int dbltext_videobase;
static int lores_videobase;
static UINT16 *artifact_map;
static UINT8 *lores_tiledata;

#define	BLACK	0
#define PURPLE	3
#define	BLUE	6
#define ORANGE	9
#define GREEN	12
#define	WHITE	15

#define PROFILER_VIDEOTOUCH PROFILER_USER3

/***************************************************************************
  helpers
***************************************************************************/

static void apple2_draw_tilemap(struct mame_bitmap *bitmap, const struct rectangle *cliprect,
	int beginrow, int endrow, struct tilemap *tm, int raw_videobase, int *tm_videobase)
{
	struct rectangle new_cliprect;

	new_cliprect = *cliprect;

	if (new_cliprect.min_y < beginrow)
		new_cliprect.min_y = beginrow;
	if (new_cliprect.max_y > endrow)
		new_cliprect.max_y = endrow;
	if (new_cliprect.min_y > new_cliprect.max_y)
		return;

	if (a2.RAMRD)
		raw_videobase += 0x10000;

	if (raw_videobase != *tm_videobase)
	{
		*tm_videobase = raw_videobase;
		tilemap_mark_all_tiles_dirty(tm);
	}
	tilemap_draw(bitmap, &new_cliprect, tm, 0, 0);
}

/***************************************************************************
  text
***************************************************************************/

static void apple2_text_gettileinfo(int memory_offset)
{
	SET_TILE_INFO(
		0,											/* gfx */
		mess_ram[text_videobase + memory_offset],	/* character */
		WHITE,										/* color */
		0)											/* flags */
}

static void apple2_dbltext_gettileinfo(int memory_offset)
{
	SET_TILE_INFO(
		1,											/* gfx */
		mess_ram[dbltext_videobase + memory_offset],/* character */
		WHITE,										/* color */
		0)											/* flags */
}

static UINT32 apple2_text_getmemoryoffset(UINT32 col, UINT32 row, UINT32 num_cols, UINT32 num_rows)
{
	/* Special Apple II addressing.  Gotta love it. */
	return (((row & 0x07) << 7) | ((row & 0x18) * 5 + col));
}

static UINT32 apple2_dbltext_getmemoryoffset(UINT32 col, UINT32 row, UINT32 num_cols, UINT32 num_rows)
{
	return apple2_text_getmemoryoffset(col / 2, row, num_cols / 2, num_rows) + ((col % 2) ? 0x800 : 0x400);
}

static void apple2_text_draw(struct mame_bitmap *bitmap, const struct rectangle *cliprect, int page, int beginrow, int endrow)
{
	if (a2.COL80)
		apple2_draw_tilemap(bitmap, cliprect, beginrow, endrow, dbltext_tilemap, 0, &dbltext_videobase);
	else
		apple2_draw_tilemap(bitmap, cliprect, beginrow, endrow, text_tilemap, page ? 0x800 : 0x400, &text_videobase);
}

/***************************************************************************
  low resolution graphics
***************************************************************************/

static void apple2_lores_gettileinfo(int memory_offset)
{
	static pen_t pal_data[2];
	int ch;

	tile_info.tile_number = 0;
	tile_info.pen_data = lores_tiledata;
	tile_info.pal_data = pal_data;
	tile_info.pen_usage = 0;
	tile_info.flags = 0;

	ch = mess_ram[lores_videobase + memory_offset];
	pal_data[0] = (ch >> 0) & 0x0f;
	pal_data[1] = (ch >> 4) & 0x0f;
}

static void apple2_lores_draw(struct mame_bitmap *bitmap, const struct rectangle *cliprect, int page, int beginrow, int endrow)
{
	apple2_draw_tilemap(bitmap, cliprect, beginrow, endrow, lores_tilemap, page ? 0x800 : 0x400, &lores_videobase);
}

/***************************************************************************
  high resolution graphics
***************************************************************************/

static UINT32 apple2_hires_getmemoryoffset(UINT32 col, UINT32 row, UINT32 num_cols, UINT32 num_rows)
{
	/* Special Apple II addressing.  Gotta love it. */
	return apple2_text_getmemoryoffset(col, row / 8, num_cols, num_rows) | ((row & 7) << 10);
}

struct drawtask_params
{
	struct mame_bitmap *bitmap;
	UINT8 *vram;
	int beginrow;
	int rowcount;
};

static void apple2_hires_draw_task(void *param, int task_num, int task_count)
{
	struct drawtask_params *dtparams;
	struct mame_bitmap *bitmap;
	UINT8 *vram;
	int beginrow;
	int endrow;
	int row, col, b;
	int offset;
	UINT8 vram_row[42];
	UINT16 v;
	UINT16 *p;
	UINT32 w;
	UINT16 *artifact_map_ptr;

	dtparams = (struct drawtask_params *) param;

	bitmap = dtparams->bitmap;
	vram = dtparams->vram;
	beginrow	= dtparams->beginrow + (dtparams->rowcount * task_num     / task_count);
	endrow		= dtparams->beginrow + (dtparams->rowcount * (task_num+1) / task_count) - 1;

	vram_row[0] = 0;
	vram_row[41] = 0;

	for (row = beginrow; row <= endrow; row++)
	{
		for (col = 0; col < 40; col++)
		{
			offset = apple2_hires_getmemoryoffset(col, row, 0, 0);
			vram_row[1+col] = vram[offset];
		}

		p = (UINT16 *) bitmap->line[row];

		for (col = 0; col < 40; col++)
		{
			w =		(((UINT32) vram_row[col+0] & 0x7f) <<  0)
				|	(((UINT32) vram_row[col+1] & 0x7f) <<  7)
				|	(((UINT32) vram_row[col+2] & 0x7f) << 14);

			artifact_map_ptr = &artifact_map[((vram_row[col+1] & 0x80) >> 7) * 16];
			
			for (b = 0; b < 7; b++)
			{
				v = artifact_map_ptr[((w >> (b + 7-1)) & 0x07) | (((b ^ col) & 0x01) << 3)];
				*(p++) = v;
				*(p++) = v;
			}
		}
	}
}

static void apple2_hires_draw(struct mame_bitmap *bitmap, const struct rectangle *cliprect, int page, int beginrow, int endrow)
{
	struct drawtask_params dtparams;

	if (beginrow < cliprect->min_y)
		beginrow = cliprect->min_y;
	if (endrow > cliprect->max_y)
		endrow = cliprect->max_y;
	if (endrow < beginrow)
		return;

	dtparams.vram = mess_ram + (page ? 0x4000 : 0x2000);
	if (a2.RAMRD)
		dtparams.vram += 0x10000;

	dtparams.bitmap = bitmap;
	dtparams.beginrow = beginrow;
	dtparams.rowcount = (endrow + 1) - beginrow;

	osd_parallelize(apple2_hires_draw_task, &dtparams, dtparams.rowcount);
}

/***************************************************************************
  video core
***************************************************************************/

VIDEO_START( apple2 )
{
	int i;
	int j;
	UINT16 c;

	static UINT16 artifact_color_table[] =
	{
		BLACK,	PURPLE,	GREEN,	WHITE,
		BLACK,	BLUE,	ORANGE,	WHITE
	};

	text_tilemap = tilemap_create(
		apple2_text_gettileinfo,
		apple2_text_getmemoryoffset,
		TILEMAP_OPAQUE,
		7*2, 8,
		40, 24);

	dbltext_tilemap = tilemap_create(
		apple2_dbltext_gettileinfo,
		apple2_dbltext_getmemoryoffset,
		TILEMAP_OPAQUE,
		7, 8,
		80, 24);

	lores_tilemap = tilemap_create(
		apple2_lores_gettileinfo,
		apple2_text_getmemoryoffset,
		TILEMAP_OPAQUE,
		14, 8,
		40, 24);

	/* 2^3 dependent pixels * 2 color sets * 2 offsets */
	artifact_map = auto_malloc(sizeof(UINT16) * 8 * 2 * 2);

	/* 14x8 */
	lores_tiledata = auto_malloc(sizeof(UINT8) * 14 * 8);

	if (!text_tilemap || !lores_tilemap || !artifact_map)
		return 1;
	
	/* build lores_tiledata */
	memset(lores_tiledata + 0*14, 0, 4*14);
	memset(lores_tiledata + 4*14, 1, 4*14);

	/* build artifact map */
	for (i = 0; i < 8; i++)
	{
		for (j = 0; j < 2; j++)
		{
			if (i & 0x02)
			{
				if ((i & 0x05) != 0)
					c = 3;
				else
					c = j ? 2 : 1;
			}
			else
			{
				if ((i & 0x05) == 0x05)
					c = j ? 1 : 2;
				else
					c = 0;
			}
			artifact_map[ 0 + j*8 + i] = artifact_color_table[c];
			artifact_map[16 + j*8 + i] = artifact_color_table[c+4];
		}
	}

	memset(&old_a2, 0, sizeof(old_a2));
	text_videobase = lores_videobase = 0;
	return 0;
}

VIDEO_UPDATE( apple2 )
{
	int page;

	page = (a2.PAGE2>>7);

	if ((a2.TEXT != old_a2.TEXT) || (a2.MIXED != old_a2.MIXED) || (a2.HIRES != old_a2.HIRES) || (a2.COL80 != old_a2.COL80) || (a2.PAGE2 != old_a2.PAGE2))
	{
		old_a2.TEXT = a2.TEXT;
		old_a2.MIXED = a2.MIXED;
		old_a2.HIRES = a2.HIRES;
		old_a2.COL80 = a2.COL80;
		old_a2.PAGE2 = a2.PAGE2;
		tilemap_mark_all_tiles_dirty(text_tilemap);
		tilemap_mark_all_tiles_dirty(dbltext_tilemap);
		tilemap_mark_all_tiles_dirty(lores_tilemap);
	}

	if (a2.TEXT)
	{
		apple2_text_draw(bitmap, cliprect, page, 0, 191);
	}
	else if ((a2.HIRES) && (a2.MIXED))
	{
		apple2_hires_draw(bitmap, cliprect, page, 0, 159);
		apple2_text_draw(bitmap, cliprect, page, 160, 191);
	}
	else if (a2.HIRES)
	{
		apple2_hires_draw(bitmap, cliprect, page, 0, 191);
	}
	else if (a2.MIXED)
	{
		apple2_lores_draw(bitmap, cliprect, page, 0, 159);
		apple2_text_draw(bitmap, cliprect, page, 160, 191);
	}
	else
	{
		apple2_lores_draw(bitmap, cliprect, page, 0, 191);
	}
}

void apple2_video_touch(offs_t offset)
{
	profiler_mark(PROFILER_VIDEOTOUCH);
	if (offset >= text_videobase)
		tilemap_mark_tile_dirty(text_tilemap, offset - text_videobase);
	if (offset >= dbltext_videobase)
		tilemap_mark_tile_dirty(dbltext_tilemap, offset - dbltext_videobase);
	if (offset >= lores_videobase)
		tilemap_mark_tile_dirty(lores_tilemap, offset - text_videobase);
	profiler_mark(PROFILER_END);
}
