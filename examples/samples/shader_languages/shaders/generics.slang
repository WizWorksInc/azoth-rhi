// Copyright 2026 Ian Pike
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
 * Slang's own idea: an interface with implementations chosen at compile time. Chain takes any two IOp and the pair below is specialized where it is written,
 * costing nothing at run time. HLSL has no equivalent.
 */

interface IOp
{
    float Apply(float value);
}

struct Scale : IOp
{
    float factor;
    float Apply(float value) { return value * factor; }
}

struct Offset : IOp
{
    float amount;
    float Apply(float value) { return value + amount; }
}

float Chain<First : IOp, Second : IOp>(First first, Second second, float value)
{
    return second.Apply(first.Apply(value));
}

RWStructuredBuffer<float> gOutput;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    const Scale scale = { 3.0 };
    const Offset offset = { 1.0 };
    gOutput[thread.x] = Chain(scale, offset, float(thread.x));
}
