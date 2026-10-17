#include "storage.h"
#include "log.h"
#include "board.h"
#include <SD.h>
#include <SPI.h>

static SPIClass sdSPI(VSPI);
static bool mounted = false;

bool storage_mount() {
  sdSPI.begin(CYD_SD_SCK_PIN, CYD_SD_MISO_PIN, CYD_SD_MOSI_PIN, CYD_SD_CS_PIN);
  // 20 MHz first, 4 MHz for the cards that will not run at that speed (as the
  // MP3 player). max_files 2: the config file and its temp copy.
  mounted = SD.begin(CYD_SD_CS_PIN, sdSPI, 20000000, "/sd", 2);
  if (!mounted) {
    SD.end();
    mounted = SD.begin(CYD_SD_CS_PIN, sdSPI, 4000000, "/sd", 2);
  }
  if (mounted)
    Log.printf("sd      : mounted, %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));
  else
    Log.println("sd      : no card (settings come from NVS only)");
  return mounted;
}

bool storage_ok() { return mounted; }
