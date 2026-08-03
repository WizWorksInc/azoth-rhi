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
 * The macOS half of GLFW's natives and the reason this sample compiles Objective-C at all. glfwGetCocoaWindow stops at the NSWindow so the CAMetalLayer the
 * Metal backend presents through has to be made and attached here. SDL_Metal_CreateView is the one call that does all of this, which is the whole of the
 * difference between the two windowed samples.
 */

#define GLFW_EXPOSE_NATIVE_COCOA

#include "glfw_natives.hpp"

#import <AppKit/AppKit.h>
#include <GLFW/glfw3native.h>
#import <QuartzCore/CAMetalLayer.h>

namespace glfw_native
{

	void * MetalLayer(GLFWwindow * window)
	{
		NSWindow * nsWindow = glfwGetCocoaWindow(window);
		if (nsWindow == nil)
		{
			return nullptr;
		}

		NSView * view = nsWindow.contentView;
		if (view == nil)
		{
			return nullptr;
		}

		/*
		 * The RHI sets the layer's device and its drawable size, including on every resize so what is left here is the scale factor, which is what keeps the
		 * backing store at the display's real pixel count and not at point resolution.
		 */
		CAMetalLayer * layer = [CAMetalLayer layer];
		layer.contentsScale	 = nsWindow.backingScaleFactor;

		/*
		 * Order matters. The view takes ownership of the layer when it is assigned and asking for a layer backed view afterwards is what keeps the one just
		 * handed over instead of replacing it with a fresh CALayer.
		 */
		view.layer		= layer;
		view.wantsLayer = YES;

		return layer;
	}

} // namespace glfw_native
