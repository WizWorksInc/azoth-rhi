// Copyright 2026 Ian Pike
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// distributed under the License is distributed on an "AS IS" BASIS,
// See the License for the specific language governing permissions and
// limitations under the License.

#include "azoth/rhi/module/backend_module.hpp"

#include "azoth/rhi/backend/support/spin_lock.hpp"

#include <mutex>
#include <utility>

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#else
	#include <dlfcn.h>
#endif

namespace azo::rhi
{
	namespace
	{

		[[nodiscard]] SpinLock & ModuleGuard() noexcept
		{
			static SpinLock guard;
			return guard;
		}

		[[nodiscard]] void * OpenLibrary(const char * path) noexcept
		{
#ifdef _WIN32
			return static_cast<void *>(::LoadLibraryA(path));
#else
			return ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
		}

		void CloseLibrary(void * handle) noexcept
		{
			if (handle == nullptr)
			{
				return;
			}

#ifdef _WIN32
			static_cast<void>(::FreeLibrary(static_cast<HMODULE>(handle)));
#else
			static_cast<void>(::dlclose(handle));
#endif
		}

		[[nodiscard]] void * FindSymbol(void * handle, const char * name) noexcept
		{
#ifdef _WIN32
			return reinterpret_cast<void *>(::GetProcAddress(static_cast<HMODULE>(handle), name));
#else
			return ::dlsym(handle, name);
#endif
		}

	}

	BackendModule::BackendModule(BackendModule && other) noexcept
	{
		Adopt(std::move(other));
	}

	BackendModule & BackendModule::operator=(BackendModule && other) noexcept
	{
		if (this != &other)
		{
			static_cast<void>(Unload());
			Adopt(std::move(other));
		}

		return *this;
	}

	// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved): the members move one at a time, the handle through exchange and not a move.
	void BackendModule::Adopt(BackendModule && other) noexcept
	{
		m_handle		  = std::exchange(other.m_handle, nullptr);
		m_names			  = std::move(other.m_names);
		m_entries		  = std::move(other.m_entries);
		m_path			  = std::move(other.m_path);
		m_liveObjectCount = std::exchange(other.m_liveObjectCount, nullptr);

		other.m_names.clear();
		other.m_entries.clear();
	}

	BackendModule::~BackendModule()
	{
		static_cast<void>(Unload());
	}

	std::size_t BackendModule::LiveObjects() const noexcept
	{
		return m_liveObjectCount != nullptr ? m_liveObjectCount() : 0;
	}

	Result<BackendModule> BackendModule::Load(const std::string_view path)
	{
		detail::HostString terminated(path);

		void * handle = nullptr;
		ModuleDescription description{};

		{
			const std::scoped_lock guard(ModuleGuard());

			handle = OpenLibrary(terminated.c_str());
			if (handle == nullptr)
			{
				return Error{
					.code	 = ErrorCode::eInvalidArgument,
					.message = "the module could not be loaded",
				};
			}

			auto * describe = reinterpret_cast<ModuleEntryPoint>(FindSymbol(handle, kModuleEntryPointName));
			if (describe == nullptr)
			{
				CloseLibrary(handle);
				return Error{
					.code	 = ErrorCode::eInvalidArgument,
					.message = "the module exports no azoRhiDescribeModule entry point",
				};
			}

			if (!describe(&description))
			{
				CloseLibrary(handle);
				return Error{
					.code	 = ErrorCode::eInvalidState,
					.message = "the module declined to describe itself",
				};
			}

			if (!(description.stamp == CurrentAbiStamp()))
			{
				CloseLibrary(handle);
				return Error{
					.code	 = ErrorCode::eIncompatibleAbi,
					.message = "the module was built against a different ABI, so nothing in it was called",
				};
			}

			if (description.entries == nullptr || description.entryCount == 0)
			{
				CloseLibrary(handle);
				return Error{
					.code	 = ErrorCode::eInvalidState,
					.message = "the module described no backends",
				};
			}
		}

		BackendModule loaded;
		loaded.m_handle			 = handle;
		loaded.m_path			 = std::move(terminated);
		loaded.m_liveObjectCount = description.liveObjectCount;

		loaded.m_names.reserve(description.entryCount * 2);
		loaded.m_entries.reserve(description.entryCount);

		for (std::size_t index = 0; index < description.entryCount; ++index)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): a count and a pointer is what crosses the boundary.
			const BackendEntry & theirs = description.entries[index];

			loaded.m_names.emplace_back(theirs.canonicalName);
			loaded.m_names.emplace_back(theirs.displayName);
		}

		for (std::size_t index = 0; index < description.entryCount; ++index)
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): a count and a pointer is what crosses the boundary.
			BackendEntry mine = description.entries[index];

			mine.canonicalName = loaded.m_names[index * 2];
			mine.displayName   = loaded.m_names[(index * 2) + 1];
			loaded.m_entries.push_back(mine);
		}

		return loaded;
	}

	Result<void> BackendModule::Unload()
	{
		const std::scoped_lock guard(ModuleGuard());

		if (m_handle == nullptr)
		{
			return {};
		}

		if (LiveObjects() != 0)
		{
			return Error{
				.code	 = ErrorCode::eInvalidState,
				.message = "the module still has live objects, so it was not unloaded",
			};
		}

		m_entries.clear();
		m_names.clear();
		m_liveObjectCount = nullptr;

		CloseLibrary(std::exchange(m_handle, nullptr));
		return {};
	}

}
