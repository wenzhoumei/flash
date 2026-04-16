static char *fontfallbacks[] = {
	"dejavu sans",
};

#define NUMFONTSCALES 42
#define FONTSZ(x) ((int)(10.0 * powf(1.1288f, (x))))

static const char *fgcolor = "#000000";
static const char *bgcolor = "#FFFFFF";

static const float usablewidth = 0.75f;
static const float usableheight = 0.75f;

static const int defaultshuffle = 1;
static const int closemode = SAVE_FAILED;
