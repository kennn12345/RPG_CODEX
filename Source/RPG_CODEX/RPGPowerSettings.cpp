#include "RPGPowerSettings.h"

URPGPowerSettings::URPGPowerSettings()
{
	// The preliminary 1-20 ladder (normal punch vs. the standard bandit). Ranges in the design use their middle.
	const float Feet[20] = {
		0.f,	// 1  very minor response
		0.f,	// 2  small tilt / recoil
		0.f,	// 3  occasional light stagger
		0.f,	// 4  noticeable stagger, rare stumble
		0.5f,	// 5  meaningful stagger, occasional knockdown
		1.f,	// 6  increasingly forceful
		1.5f,	// 7  strong stagger, forced movement
		2.5f,	// 8  2-3 ft
		4.5f,	// 9  3-6 ft
		7.5f,	// 10 5-10 ft
		12.5f,	// 11 10-15 ft
		20.f,	// 12 15-25 ft
		30.f,	// 13 25-35 ft
		42.5f,	// 14 35-50 ft
		60.f,	// 15 50-70 ft
		80.f,	// 16 70-90 ft
		100.f,	// 17 ~100 ft
		120.f,	// 18 ~120 ft
		140.f,	// 19 ~140 ft
		150.f	// 20 150 ft+
	};

	for (int32 Index = 0; Index < 20; ++Index)
	{
		FRPGPowerLadderStep Step;
		Step.Level = float(Index + 1);
		Step.MaxDisplacementFeet = Feet[Index];
		Ladder.Add(Step);
	}
}
