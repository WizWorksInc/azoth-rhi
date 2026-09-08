// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

interface IOp
{
	float Apply(float value);
}

struct Scale : IOp
{
	float factor;

	float Apply(float value)
	{
		return value * factor;
	}
}

struct Offset : IOp

{
	float amount;

	float Apply(float value)
	{
		return value + amount;
	}

}

float Chain<First : IOp, Second : IOp>(First first, Second second, float value)
{
	return second.Apply(first.Apply(value));
}

RWStructuredBuffer<float> gOutput;

[shader("compute")][numthreads(64, 1, 1)] void computeMain(uint3 thread : SV_DispatchThreadID)
{
	const Scale scale	= { 3.0 };
	const Offset offset = { 1.0 };
	gOutput[thread.x]	= Chain(scale, offset, float(thread.x));
}
