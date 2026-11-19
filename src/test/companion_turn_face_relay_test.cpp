/*
 * Open X-Com Companion — TURN_FACE relay integration (M1 WP-TURN-1c).
 */
#include <iostream>

#ifdef main
#undef main
#endif

void runTurnFaceRelayE2eSuite();
extern int turnRelayFailures;

int main()
{
	runTurnFaceRelayE2eSuite();
	if (turnRelayFailures != 0)
	{
		return 1;
	}
	std::cout << "companion_turn_face_relay_test: all tests passed." << std::endl;
	return 0;
}