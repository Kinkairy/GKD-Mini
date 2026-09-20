#include <string.h>

int gkdu_engine_main(int argc, char **argv);
int gkdu_coordinator_main(int argc, char **argv);

int main(int argc, char **argv)
{
	const char *name = argv && argv[0] ? strrchr(argv[0], '/') : 0;
	name = name ? name + 1 : (argv && argv[0] ? argv[0] : "");
	if (!strcmp(name, "gkd-update-coordinator"))
		return gkdu_coordinator_main(argc, argv);
	return gkdu_engine_main(argc, argv);
}
