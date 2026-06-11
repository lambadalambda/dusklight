// Tiny extractor: dumps files matching a substring from a GC disc image via libnod.
// usage: nodx <image> <name-substring> <outdir>   (or substring "LIST" to just list)
#include <nod.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct NodHandle* g_part;
static const char* g_match;
static const char* g_outdir;

struct DirFrame { uint32_t end; char name[128]; };
static struct DirFrame g_stack[32];
static int g_depth;

static uint32_t cb(uint32_t index, enum NodNodeKind kind, const char* name, uint32_t size, void* ud) {
  (void)ud;
  while (g_depth > 0 && index >= g_stack[g_depth - 1].end)
    g_depth--;

  char full[1024] = {0};
  for (int i = 0; i < g_depth; i++) {
    strcat(full, g_stack[i].name);
    strcat(full, "/");
  }
  strcat(full, name);

  if (kind == NOD_NODE_KIND_DIRECTORY) {
    if (g_depth < 32) {
      g_stack[g_depth].end = size;
      snprintf(g_stack[g_depth].name, sizeof(g_stack[g_depth].name), "%s", name);
      g_depth++;
    }
    return index + 1;
  }

  if (strstr(full, g_match)) {
    printf("%s (%u bytes)\n", full, size);
    if (strcmp(g_outdir, "LIST") != 0) {
      struct NodHandle* f = NULL;
      if (nod_partition_open_file(g_part, index, &f) == NOD_RESULT_OK) {
        char out[1200];
        char flat[1024];
        snprintf(flat, sizeof(flat), "%s", full);
        for (char* p = flat; *p; p++)
          if (*p == '/') *p = '_';
        snprintf(out, sizeof(out), "%s/%s", g_outdir, flat);
        FILE* fp = fopen(out, "wb");
        uint8_t buf[65536];
        int64_t n;
        while ((n = nod_read(f, buf, sizeof(buf))) > 0)
          fwrite(buf, 1, (size_t)n, fp);
        fclose(fp);
        nod_free(f);
      } else {
        fprintf(stderr, "open failed: %s\n", nod_error_message());
      }
    }
  }
  return index + 1;
}

int main(int argc, char** argv) {
  if (argc != 4) { fprintf(stderr, "usage: nodx <image> <substr> <outdir|LIST>\n"); return 2; }
  g_match = argv[2];
  g_outdir = argv[3];
  struct NodHandle* disc = NULL;
  if (nod_disc_open(argv[1], NULL, &disc) != NOD_RESULT_OK) {
    fprintf(stderr, "disc open failed: %s\n", nod_error_message());
    return 1;
  }
  if (nod_disc_open_partition_kind(disc, NOD_PARTITION_KIND_DATA, NULL, &g_part) != NOD_RESULT_OK) {
    fprintf(stderr, "partition open failed: %s\n", nod_error_message());
    return 1;
  }
  nod_partition_iterate_fst(g_part, cb, NULL);
  nod_free(g_part);
  nod_free(disc);
  return 0;
}
