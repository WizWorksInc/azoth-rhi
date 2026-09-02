// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

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

		CAMetalLayer * layer = [CAMetalLayer layer];
		layer.contentsScale	 = nsWindow.backingScaleFactor;

		view.layer		= layer;
		view.wantsLayer = YES;

		return layer;
	}

}
