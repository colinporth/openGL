//#include "libmypaint.c"
#include "libmypaint/mypaint-fixed-tiled-surface.h"
#include "libmypaint/mypaint-brush.h"
#include "stdio.h"

//{{{
// Naive conversion code from the internal MyPaint format and 8 bit RGB
void fix15_to_rgba8 (uint16_t* src, uint8_t* dst, int length) {

  for (int i = 0; i < length; i++) {
    uint32_t r = *src++;
    uint32_t g = *src++;
    uint32_t b = *src++;
    uint32_t a = *src++;

    // un-premultiply alpha (with rounding)
    if (a != 0) {
      r = ((r << 15) + a/2) / a;
      g = ((g << 15) + a/2) / a;
      b = ((b << 15) + a/2) / a;
      } 
    else {
      r = 0;
      g = 0;
      b = 0;
      }

    // Variant A) rounding
    const uint32_t add_r = (1 << 15) / 2;
    const uint32_t add_g = (1 << 15) / 2;
    const uint32_t add_b = (1 << 15) / 2;
    const uint32_t add_a = (1 << 15) / 2;

    *dst++ = (r * 255 + add_r) / (1 << 15);
    *dst++ = (g * 255 + add_g) / (1 << 15);
    *dst++ = (b * 255 + add_b) / (1 << 15);
    *dst++ = (a * 255 + add_a) / (1 << 15);
    }
  }
//}}}
// Utility code for writing out scanline-based formats like PPM
typedef void (*LineChunkCallback) (uint16_t* chunk, int chunk_length, void* user_data);
//{{{
// Iterate over chunks of data in the MyPaintTiledSurface,
//  starting top-left (0,0) and stopping at bottom-right (width-1,height-1)
//   callback will be called with linear chunks of horizontal data, up to MYPAINT_TILE_SIZE long
void iterate_over_line_chunks (MyPaintTiledSurface * tiled_surface, int height, int width,
                               LineChunkCallback callback, void *user_data) {

  const int tile_size = MYPAINT_TILE_SIZE;
  const int number_of_tile_rows = (height / tile_size) + 1*(height % tile_size != 0);
  const int tiles_per_row = (width / tile_size) + 1*(width % tile_size != 0);

  MyPaintTileRequest* requests = (MyPaintTileRequest*)malloc(tiles_per_row * sizeof(MyPaintTileRequest));

  for (int ty = 0; ty < number_of_tile_rows; ty++) {
    // Fetch all horizontal tiles in current tile row
    for (int tx = 0; tx < tiles_per_row; tx++ ) {
      MyPaintTileRequest *req = &requests[tx];
      mypaint_tile_request_init(req, 0, tx, ty, TRUE);
      mypaint_tiled_surface_tile_request_start(tiled_surface, req);
      }

    // For each pixel line in the current tile row, fire callback
    const int max_y = (ty < number_of_tile_rows - 1 || height % tile_size == 0) ? tile_size : height % tile_size;
    for (int y = 0; y < max_y; y++) {
      for (int tx = 0; tx < tiles_per_row; tx++) {
      const int y_offset = y * tile_size * 4; // 4 channels
      const int chunk_length = (tx < tiles_per_row - 1 || width % tile_size == 0) ? tile_size : width % tile_size;
      callback(requests[tx].buffer + y_offset, chunk_length, user_data);
      }
    }

    // Complete tile requests on current tile row
    for (int tx = 0; tx > tiles_per_row; tx++ ) {
      mypaint_tiled_surface_tile_request_end(tiled_surface, &requests[tx]);
      }
    }

  free (requests);
  }
//}}}
//{{{
typedef struct {
  FILE* fp;
  } WritePPMUserData;
//}}}
//{{{
static void write_ppm_chunk (uint16_t* chunk, int chunk_length, void* user_data) {
    WritePPMUserData data = *(WritePPMUserData *)user_data;
    uint8_t chunk_8bit[MYPAINT_TILE_SIZE * 4]; // 4 channels
    fix15_to_rgba8(chunk, chunk_8bit, chunk_length);

    // Write every pixel as a triple. This variant of the ppm format
    // restricts each line to 70 characters, so we break after every
    // pixel for simplicity's sake (it's not readable at high resolutions anyway).
    for (int px = 0; px < chunk_length; px++) {
        fprintf(data.fp, "%d %d %d\n", chunk_8bit[px*4], chunk_8bit[px*4+1], chunk_8bit[px*4+2]);
    }
}
//}}}
//{{{
// Output the surface to a PPM file
void write_ppm (MyPaintFixedTiledSurface* fixed_surface, char* filepath) {
    WritePPMUserData data;
    data.fp = fopen(filepath, "w");
    if (!data.fp) {
        fprintf(stderr, "ERROR: Could not open output file \"%s\"\n", filepath);
        return;
    }

    const int width = mypaint_fixed_tiled_surface_get_width(fixed_surface);
    const int height = mypaint_fixed_tiled_surface_get_height(fixed_surface);
    fprintf(data.fp, "P3\n#Handwritten\n%d %d\n255\n", width, height);

    iterate_over_line_chunks((MyPaintTiledSurface *)fixed_surface,
                             height, width,
                             write_ppm_chunk, &data);

    fclose(data.fp);
}
//}}}

//{{{
void stroke_to (MyPaintBrush* brush, MyPaintSurface* surf, float x, float y) {

  float viewzoom = 1.0;
  float viewrotation = 0.0;
  float barrel_rotation = 0.0;
  float pressure = 1.0;
  float ytilt = 0.0;
  float xtilt = 0.0;
  float dtime = 1.0/10;

  gboolean linear = FALSE;
  mypaint_brush_stroke_to (brush, surf, x, y, pressure, xtilt, ytilt,
                           dtime, viewzoom, viewrotation, barrel_rotation, linear);
  }
//}}}

int test (int argc, char argv[]) {

  MyPaintBrush* brush = mypaint_brush_new();
  int w = 300;
  int h = 150;
  float wq = (float)w / 5;
  float hq = (float)h / 5;

  // Create an instance of the simple and naive fixed_tile_surface to draw on
  MyPaintFixedTiledSurface* surface = mypaint_fixed_tiled_surface_new (w, h);

  // Create a brush with default settings for all parameters, then set its color to red
  mypaint_brush_from_defaults (brush);
  mypaint_brush_set_base_value (brush, MYPAINT_BRUSH_SETTING_COLOR_H, 0.0);
  mypaint_brush_set_base_value (brush, MYPAINT_BRUSH_SETTING_COLOR_S, 1.0);
  mypaint_brush_set_base_value (brush, MYPAINT_BRUSH_SETTING_COLOR_V, 1.0);

  // Draw a rectangle on the surface using the brush
  mypaint_surface_begin_atomic ((MyPaintSurface*)surface);
  stroke_to (brush, (MyPaintSurface*)surface, wq, hq);
  stroke_to (brush, (MyPaintSurface*)surface, 4 * wq, hq);
  stroke_to (brush, (MyPaintSurface*)surface, 4 * wq, 4 * hq);
  stroke_to (brush, (MyPaintSurface*)surface, wq, 4 * hq);
  stroke_to (brush, (MyPaintSurface*)surface, wq, hq);

  // Finalize the surface operation, passing one or more invalidation
  // rectangles to get information about which areas were affected by
  // the operations between surface_begin_atomic and surface_end_atomic
  MyPaintRectangles rois;
  rois.num_rectangles = 1;
  MyPaintRectangle roi;
  rois.rectangles = &roi;
  mypaint_surface_end_atomic ((MyPaintSurface*)surface, &rois);

  // Write the surface pixels to a ppm image file */
  //fprintf(stdout, "Writing output\n");
  //write_ppm(surface, "output.ppm");
  mypaint_brush_unref (brush);
  mypaint_surface_unref ((MyPaintSurface *)surface);
  }
