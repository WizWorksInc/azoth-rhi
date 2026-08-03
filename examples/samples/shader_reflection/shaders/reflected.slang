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
 * The shader the host builds its pipeline layout out of and not beside.
 *
 * Nothing in here is written twice. The three resources below, the numbers they sit at and the size of the group are read back off the compiled program, so
 * moving a binding or adding a resource changes the host with it. What the host does hard code is the contract: a scale, a bias and a run of floats.
 */

struct Params
{
    float scale;
    float bias;
};

/*
 * A push constant and a set, the shape this RHI's Metal ABI is built around: buffer 0 is reserved for the push constant whether or not a layout declares one,
 * and a descriptor set is an argument buffer above that.
 *
 * Slang leaves buffer 0 free only when the shader declares a push constant, so a shader without one numbers its sets one too low. Pipeline creation refuses
 * that, and declaring the push constant is what makes this run.
 */
[[vk::push_constant]]
ConstantBuffer<Params> gParams;

// Two kinds on purpose, since telling them apart is the part of reflection with any work in it. A read only structured buffer and a writable one lower to
// different descriptor types and Slang reports each one differently.
struct Resources
{
    StructuredBuffer<float> input;
    RWStructuredBuffer<float> output;
};

ParameterBlock<Resources> gSet;

[shader("compute")]
[numthreads(32, 1, 1)]
void transformMain(uint3 thread : SV_DispatchThreadID)
{
    gSet.output[thread.x] = (gSet.input[thread.x] * gParams.scale) + gParams.bias;
}
