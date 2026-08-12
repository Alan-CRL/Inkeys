export module Inkeys.Business.ComponentActions;

export namespace Inkeys::Business
{
	enum class BuiltInComponentAction
	{
		Explorer,
		TaskManager,
		ControlPanel,
		ShowDesktop,
		LockWorkStation,
		Escape,
		AltF4,
		IslandCaller,
		IslandCallerSimple,
		SecRandomDirect,
		SecRandomQuickDraw,
		SecRandomQuickDrawCompat,
		NamePicker,
		ClassIslandSettings,
		ClassIslandProfile,
		ClassIslandClassSwap,
	};

	void ExecuteBuiltInComponentAction(BuiltInComponentAction action);
	void ShutdownComponentActions();
}
