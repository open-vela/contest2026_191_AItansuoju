/****************************************************************************
 * 6-inch e-paper todo application
 *
 * The SPI peer is deliberately a small command receiver. Rendering and
 * panel-specific waveforms stay on the e-paper driver board.
 ****************************************************************************/

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef CONFIG_EPAPER_TODO_SPI_DEV
#  define CONFIG_EPAPER_TODO_SPI_DEV "/dev/spidev0.0"
#endif

#define TODO_MAX 32
#define TODO_TEXT_MAX 96
#define FRAME_MAX 2048
#define SOF0 0xa5
#define SOF1 0x5a
#define PROTO_VERSION 1
#define CMD_REFRESH 0x10
#define CMD_CLEAR 0x11

struct todo_item { int id; int done; char text[TODO_TEXT_MAX]; };
static struct todo_item g_items[TODO_MAX];
static int g_count;
static int g_next_id = 1;

static uint16_t crc16(const uint8_t *p, size_t n)
{
  uint16_t crc = 0xffff;
  while (n--) { crc ^= *p++; for (int i = 0; i < 8; i++) crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1; }
  return crc;
}

/* Frame: A5 5A version command sequence length_be payload crc16_le. */
static int spi_frame(uint8_t command, const uint8_t *payload, size_t length)
{
  uint8_t frame[FRAME_MAX];
  uint16_t crc;
  int fd;
  if (length > FRAME_MAX - 9) return -1;
  frame[0] = SOF0; frame[1] = SOF1; frame[2] = PROTO_VERSION;
  frame[3] = command; frame[4] = 0;
  frame[5] = (uint8_t)(length >> 8); frame[6] = (uint8_t)length;
  if (length) memcpy(frame + 7, payload, length);
  crc = crc16(frame + 2, length + 5);
  frame[7 + length] = (uint8_t)crc; frame[8 + length] = (uint8_t)(crc >> 8);
  fd = open(CONFIG_EPAPER_TODO_SPI_DEV, O_WRONLY);
  if (fd < 0) return -1;
  int written = (int)write(fd, frame, length + 9);
  close(fd);
  return written == (int)(length + 9) ? 0 : -1;
}

static int refresh(void)
{
  char body[FRAME_MAX];
  int used = snprintf(body, sizeof(body), "TODOLIST\\n");
  for (int i = 0; i < g_count && used > 0 && used < (int)sizeof(body); i++)
    used += snprintf(body + used, sizeof(body) - used, "%d|%c|%s\\n", g_items[i].id, g_items[i].done ? 'x' : ' ', g_items[i].text);
  return used < 0 || used >= (int)sizeof(body) ? -1 : spi_frame(CMD_REFRESH, (uint8_t *)body, (size_t)used);
}

static struct todo_item *find_item(int id)
{ for (int i = 0; i < g_count; i++) if (g_items[i].id == id) return &g_items[i]; return NULL; }

static int add_item(const char *text)
{
  if (!text || !*text || g_count >= TODO_MAX) return -1;
  g_items[g_count].id = g_next_id++; g_items[g_count].done = 0;
  strncpy(g_items[g_count].text, text, TODO_TEXT_MAX - 1); g_items[g_count].text[TODO_TEXT_MAX - 1] = '\0';
  return g_items[g_count++].id;
}

static int delete_item(int id)
{
  for (int i = 0; i < g_count; i++) if (g_items[i].id == id) { g_items[i] = g_items[g_count - 1]; g_count--; return 0; }
  return -1;
}

/* Small JSON string extractor, sufficient for MCP tool arguments. */
static int json_string(const char *json, const char *key, char *out, size_t cap)
{
  char needle[48]; const char *p; size_t n = 0;
  snprintf(needle, sizeof(needle), "\"%s\"", key); p = strstr(json, needle); if (!p) return -1;
  p = strchr(p + strlen(needle), ':'); if (!p) return -1; p++; while (*p == ' ' || *p == '\t') p++; if (*p != '\"') return -1; p++;
  while (*p && *p != '\"' && n + 1 < cap) { if (*p == '\\' && p[1]) p++; out[n++] = *p++; }
  out[n] = '\0'; return *p == '\"' ? 0 : -1;
}

static void json_id(const char *json, char *out, size_t cap)
{
  char needle[48]; const char *p; size_t n = 0;
  snprintf(needle, sizeof(needle), "\"id\""); p = strstr(json, needle);
  if (!p) { strncpy(out, "null", cap); return; }
  p = strchr(p + strlen(needle), ':'); if (!p) { strncpy(out, "null", cap); return; }
  p++; while (*p == ' ' || *p == '\t') p++;
  if (*p == '"') { p++; while (*p && *p != '"' && n + 1 < cap) out[n++] = *p++; }
  else while (*p && *p != ',' && *p != '}' && n + 1 < cap) out[n++] = *p++;
  while (n && (out[n - 1] == ' ' || out[n - 1] == '\t')) n--; out[n] = '\0';
}

static int json_int(const char *json, const char *key, int *value)
{
  char needle[48]; const char *p; snprintf(needle, sizeof(needle), "\"%s\"", key); p = strstr(json, needle); if (!p) return -1;
  p = strchr(p + strlen(needle), ':'); if (!p) return -1; *value = atoi(p + 1); return 0;
}

static void mcp_reply(const char *id, const char *result)
{ printf("{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":%s}\n", id ? id : "null", result); fflush(stdout); }

static void mcp_line(char *line)
{
  char id[32] = "null", text[TODO_TEXT_MAX], method[48]; int value, item_id;
  json_id(line, id, sizeof(id));
  if (json_string(line, "method", method, sizeof(method)) < 0) { mcp_reply(id, "{\"error\":\"method required\"}"); return; }
  if (!strcmp(method, "initialize")) { mcp_reply(id, "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{}}"); return; }
  if (!strcmp(method, "tools/list")) { mcp_reply(id, "{\"tools\":[{\"name\":\"todo_add\"},{\"name\":\"todo_update\"},{\"name\":\"todo_delete\"},{\"name\":\"todo_refresh\"}]}"); return; }
  if (!strcmp(method, "tools/call")) {
    if (json_string(line, "name", method, sizeof(method)) < 0) { mcp_reply(id, "{\"error\":\"tool name required\"}"); return; }
    if (!strcmp(method, "todo_add") && json_string(line, "text", text, sizeof(text)) == 0) { value = add_item(text); if (value > 0) { refresh(); char r[64]; snprintf(r, sizeof(r), "{\"id\":%d}", value); mcp_reply(id, r); } else mcp_reply(id, "{\"error\":\"list full or empty text\"}"); return; }
    if (!strcmp(method, "todo_update") && json_int(line, "id", &item_id) == 0) { struct todo_item *it = find_item(item_id); if (it) { if (json_string(line, "text", text, sizeof(text)) == 0) { strncpy(it->text, text, TODO_TEXT_MAX - 1); it->text[TODO_TEXT_MAX - 1] = '\0'; } if (json_int(line, "done", &value) == 0) it->done = value != 0; refresh(); mcp_reply(id, "{\"ok\":true}"); } else mcp_reply(id, "{\"error\":\"todo not found\"}"); return; }
    if (!strcmp(method, "todo_delete") && json_int(line, "id", &item_id) == 0) { if (delete_item(item_id) == 0) { refresh(); mcp_reply(id, "{\"ok\":true}"); } else mcp_reply(id, "{\"error\":\"todo not found\"}"); return; }
    if (!strcmp(method, "todo_refresh")) { mcp_reply(id, refresh() == 0 ? "{\"ok\":true}" : "{\"error\":\"SPI write failed\"}"); return; }
  }
  mcp_reply(id, "{\"error\":\"unsupported method\"}");
}

int main(int argc, char *argv[])
{
  char line[512];
  if (argc > 1 && !strcmp(argv[1], "mcp")) { while (fgets(line, sizeof(line), stdin)) mcp_line(line); return 0; }
  if (argc > 1 && !strcmp(argv[1], "refresh")) return refresh();
  if (argc > 2 && !strcmp(argv[1], "add")) { int id = add_item(argv[2]); if (id < 0) return 1; return refresh(); }
  if (argc > 2 && !strcmp(argv[1], "done")) { struct todo_item *it = find_item(atoi(argv[2])); if (!it) return 1; it->done = 1; return refresh(); }
  printf("usage: epaper_todo mcp | add <text> | done <id> | refresh\n"); return 0;
}
