// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/**
 * The public interface to this module
 */
class IViewportInteractionModule : public IModuleInterface
{

public:

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IViewportInteractionModule& Get()
	{
		return FModuleManager::LoadModuleChecked< IViewportInteractionModule >( "ViewportInteraction" );
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded( "ViewportInteraction" );
	}

	/**
	 * @return The ViewportWorldInteractionManager that owns entering and exiting the ViewportWorldInteraction
	 */
	virtual class FViewportWorldInteractionManager& GetWorldInteractionManager() = 0;

	/**
	 * Updates the ViewportWorldInteraction
	 */
	virtual void Tick( float DeltaTime ) = 0;
};
