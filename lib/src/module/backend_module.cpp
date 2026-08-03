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

		/*
		 * Load and unload are serialized against each other, a host loading modules from a worker thread being the case this serves. The plain spin lock, not
		 * any device's SyncOps, the same category as the catalog and the host allocator: this is process scoped.
		 *
		 * It guards the platform loader calls as well as our own bookkeeping. dlopen and LoadLibrary are thread safe themselves, but the refcount check and the
		 * unload after it are not one operation without this.
		 */
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
			// Local, not global so two modules that happen to define the same symbol do not resolve into each other and NOW so a missing symbol is a failure to load and
			// not a fault on the first call through it.
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

	} // namespace

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
		// A refusal here has nowhere to go so a module still holding live objects stays loaded. Leaking an image is recoverable and pulling one out from under a live
		// device is not.
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

		/*
		 * The guard covers opening the image and asking it what it is and stops there.
		 *
		 * A BackendModule must not be destroyed while it is held because the destructor calls Unload, which takes the same lock and the lock is not recursive.
		 * Holding the guard across the whole function deadlocks every load, since the local built on success is moved into the Result and then destructed before the
		 * guard is. Nothing below the block touches shared state.
		 */
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

			/*
			 * The entry point fills the stamp before anything else runs and the stamp is what is read first.
			 *
			 * This is the one call made into a module whose layout has not been agreed, which is why ModuleDescription is a plain struct of scalars and pointers and not
			 * anything carrying a span: the description has to be readable by a host that has not yet decided the module is compatible.
			 */
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

		/*
		 * Names copied before the entries are built and both reserved first.
		 *
		 * An entry carries two string views. If the storage they point at moved afterwards, every entry already built would point at freed memory and the failure
		 * would be a dangling name and not a missing one. Reserving from the count the module reported is what makes the copies stable and it is why the entries are
		 * reserved too.
		 */
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
			// Already gone. Succeeding without complaining so a host tearing down in a loop does not have to track which of its modules it has already released.
			return {};
		}

		if (LiveObjects() != 0)
		{
			return Error{
				.code	 = ErrorCode::eInvalidState,
				.message = "the module still has live objects, so it was not unloaded",
			};
		}

		/*
		 * The names go before the image does.
		 *
		 * Every entry points into m_names and not into the module so nothing here reaches into the image being closed. Clearing first is still the right order: after
		 * this returns, an entry a caller kept is a dangling pointer either way and leaving the storage alive would make it a dangling pointer that still resolves.
		 */
		m_entries.clear();
		m_names.clear();
		m_liveObjectCount = nullptr;

		CloseLibrary(std::exchange(m_handle, nullptr));
		return {};
	}

} // namespace azo::rhi
