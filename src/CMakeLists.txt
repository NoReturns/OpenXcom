set ( root_src
  lodepng.cpp
  main.cpp
)

set ( basescape_src
  Basescape/BaseInfoState.cpp
  Basescape/BaseView.cpp
  Basescape/BasescapeState.cpp
  Basescape/BuildFacilitiesState.cpp
  Basescape/CraftArmorState.cpp
  Basescape/CraftEquipmentState.cpp
  Basescape/CraftInfoState.cpp
  Basescape/CraftSoldiersState.cpp
  Basescape/CraftWeaponsState.cpp
  Basescape/CraftsState.cpp
  Basescape/DismantleFacilityState.cpp
  Basescape/ManageAlienContainmentState.cpp
  Basescape/ManufactureInfoState.cpp
  Basescape/ManufactureStartState.cpp
  Basescape/ManufactureState.cpp
  Basescape/MiniBaseView.cpp
  Basescape/MonthlyCostsState.cpp
  Basescape/NewManufactureListState.cpp
  Basescape/NewResearchListState.cpp
  Basescape/PlaceFacilityState.cpp
  Basescape/PlaceLiftState.cpp
  Basescape/PlaceStartFacilityState.cpp
  Basescape/PurchaseState.cpp
  Basescape/ResearchInfoState.cpp
  Basescape/ResearchState.cpp
  Basescape/SackSoldierState.cpp
  Basescape/SelectStartFacilityState.cpp
  Basescape/SellState.cpp
  Basescape/SoldierArmorState.cpp
  Basescape/SoldierDiaryMissionState.cpp
  Basescape/SoldierDiaryOverviewState.cpp
  Basescape/SoldierDiaryPerformanceState.cpp
  Basescape/SoldierInfoState.cpp
  Basescape/SoldierMemorialState.cpp
  Basescape/SoldiersState.cpp
  Basescape/StoresState.cpp
  Basescape/TransferBaseState.cpp
  Basescape/TransferConfirmState.cpp
  Basescape/TransferItemsState.cpp
  Basescape/TransfersState.cpp
)

set ( battlescape_src
  Battlescape/AbortMissionState.cpp
  Battlescape/ActionMenuItem.cpp
  Battlescape/ActionMenuState.cpp
  Battlescape/AliensCrashState.cpp
  Battlescape/AIModule.cpp
  Battlescape/BattleState.cpp
  Battlescape/BattlescapeGame.cpp
  Battlescape/BattlescapeGenerator.cpp
  Battlescape/BattlescapeMessage.cpp
  Battlescape/BattlescapeState.cpp
  Battlescape/CompanionSquadOwnershipState.cpp
  Battlescape/BriefingState.cpp
  Battlescape/Camera.cpp
  Battlescape/CannotReequipState.cpp
  Battlescape/CommendationLateState.cpp
  Battlescape/CommendationState.cpp
  Battlescape/DebriefingState.cpp
  Battlescape/Explosion.cpp
  Battlescape/ExplosionBState.cpp
  Battlescape/InfoboxOKState.cpp
  Battlescape/InfoboxState.cpp
  Battlescape/Inventory.cpp
  Battlescape/InventoryState.cpp
  Battlescape/Map.cpp
  Battlescape/MedikitState.cpp
  Battlescape/MedikitView.cpp
  Battlescape/MeleeAttackBState.cpp
  Battlescape/MiniMapState.cpp
  Battlescape/MiniMapView.cpp
  Battlescape/NextTurnState.cpp
  Battlescape/Particle.cpp
  Battlescape/Pathfinding.cpp
  Battlescape/PathfindingNode.cpp
  Battlescape/PathfindingOpenSet.cpp
  Battlescape/PrimeGrenadeState.cpp
  Battlescape/Projectile.cpp
  Battlescape/ProjectileFlyBState.cpp
  Battlescape/PromotionsState.cpp
  Battlescape/PsiAttackBState.cpp
  Battlescape/ScannerState.cpp
  Battlescape/ScannerView.cpp
  Battlescape/TileEngine.cpp
  Battlescape/UnitDieBState.cpp
  Battlescape/UnitFallBState.cpp
  Battlescape/UnitInfoState.cpp
  Battlescape/UnitPanicBState.cpp
  Battlescape/UnitSprite.cpp
  Battlescape/UnitTurnBState.cpp
  Battlescape/UnitWalkBState.cpp
  Battlescape/WarningMessage.cpp
)

set ( engine_src
  Engine/Action.cpp
  Engine/Adlib/adlplayer.cpp
  Engine/Adlib/fmopl.cpp
  Engine/AdlibMusic.cpp
  Engine/CatFile.cpp
  Engine/CompanionEventLogCore.cpp
	Engine/CompanionEventLog.cpp
  Engine/CompanionPerceptionLogCore.cpp
  Engine/CompanionUniformShootingSpreadCore.cpp
	Engine/CompanionPerceptionLog.cpp
	Engine/CompanionGroundItemObservationCore.cpp
	Engine/CompanionItemResearchEligibilityCore.cpp
	Engine/CompanionGroundItemMemory.cpp
	Engine/CompanionSnapshotGroundItems.cpp
	Engine/CompanionGroundInventoryCore.cpp
	Engine/CompanionSnapshot.cpp
	Engine/CompanionGithubQuotaHudCore.cpp
	Engine/CompanionTacticalHoverCoordsCore.cpp
	Engine/CompanionGithubQuotaHudOverlay.cpp
	Engine/CompanionAutoRetrieveCore.cpp
	Engine/CompanionOnDemandRelayCore.cpp
	Engine/CompanionStrategicSendEnqueueSupport.cpp
	Engine/CompanionOnDemandRelayLayout.cpp
	Engine/CompanionOnDemandRelayOverlay.cpp
	Engine/CompanionRelayPublicationFreshnessCore.cpp
	Engine/CompanionSnapshotExportCore.cpp
	Engine/CompanionManualV5Export.cpp
	Engine/CompanionManualUploadHudCore.cpp
	Engine/CompanionManualExportRetention.cpp
	Engine/CompanionSnapshotTerrain.cpp
	Engine/CompanionSnapshotTerrainCapture.cpp
	Engine/CompanionSnapshotTerrainPacked.cpp
	Engine/CompanionSnapshotTerrainSemantics.cpp
	Engine/CompanionSnapshotTerrainSemanticsSparse.cpp
	Engine/CompanionSnapshotMissionContext.cpp
	Engine/CompanionSnapshotUnitState.cpp
	Engine/CompanionSnapshotUnitStateCore.cpp
	Engine/CompanionSha256.cpp
	Engine/CompanionBriefingRequest.cpp
	Engine/CompanionBriefingRequestCore.cpp
	Engine/CompanionCommandCore.cpp
	Engine/CompanionCommandRuntime.cpp
	Engine/CompanionCommandRuntimeSupport.cpp
	Engine/CompanionCommandCasualtySettlement.cpp
	Engine/CompanionCommandCasualtySettlementCore.cpp
	Engine/CompanionCommandJournalCore.cpp
	Engine/CompanionJsonLite.cpp
	Engine/CompanionSoldierOwnershipCore.cpp
	Engine/CompanionCampaignIdentityCore.cpp
	Engine/CompanionCampaignOwnershipCore.cpp
	Engine/CompanionTacticalOwnershipCore.cpp
	Engine/CompanionTacticalAuthorityCore.cpp
	Engine/CompanionTacticalHumanOrderAdmission.cpp
	Engine/CompanionBattlescapeOwnershipCore.cpp
	Engine/CompanionOwnershipHandoffCore.cpp
	Engine/CompanionSoldierControlPublishCore.cpp
	Engine/CompanionDropGroundPlacementCore.cpp
	Engine/CompanionRulesReferenceExportCore.cpp
	Engine/CompanionRulesReferenceExport.cpp
	Engine/CompanionBaseIntelligenceExportCore.cpp
	Engine/CompanionBaseIntelligencePurchaseCatalogCore.cpp
	Engine/CompanionBaseIntelligenceExport.cpp
  Engine/CrossPlatform.cpp
  Engine/FastLineClip.cpp
  Engine/FileMap.cpp
  Engine/FlcPlayer.cpp
  Engine/Font.cpp
  Engine/GMCat.cpp
  Engine/Game.cpp
  Engine/InteractiveSurface.cpp
  Engine/Language.cpp
  Engine/LanguagePlurality.cpp
  Engine/LocalizedText.cpp
  Engine/ModInfo.cpp
  Engine/Music.cpp
  Engine/OpenGL.cpp
  Engine/OptionInfo.cpp
  Engine/Options.cpp
  Engine/Palette.cpp
  Engine/RNG.cpp
  Engine/Scalers/hq2x.cpp
  Engine/Scalers/hq3x.cpp
  Engine/Scalers/hq4x.cpp
  Engine/Scalers/init.cpp
  Engine/Scalers/scale2x.cpp
  Engine/Scalers/scale3x.cpp
  Engine/Scalers/scalebit.cpp
  Engine/Scalers/xbrz.cpp
  Engine/Screen.cpp
  Engine/Sound.cpp
  Engine/SoundSet.cpp
  Engine/State.cpp
  Engine/Surface.cpp
  Engine/SurfaceSet.cpp
  Engine/Timer.cpp
  Engine/Unicode.cpp
  Engine/Zoom.cpp
)

set ( geoscape_src
  Geoscape/AlienBaseState.cpp
  Geoscape/AllocatePsiTrainingState.cpp
  Geoscape/BaseDefenseState.cpp
  Geoscape/BaseDestroyedState.cpp
  Geoscape/BaseNameState.cpp
  Geoscape/BuildNewBaseState.cpp
  Geoscape/ConfirmCydoniaState.cpp
  Geoscape/ConfirmDestinationState.cpp
  Geoscape/ConfirmLandingState.cpp
  Geoscape/ConfirmNewBaseState.cpp
  Geoscape/CraftErrorState.cpp
  Geoscape/CraftPatrolState.cpp
  Geoscape/DogfightErrorState.cpp
  Geoscape/DogfightState.cpp
  Geoscape/FundingState.cpp
  Geoscape/GeoscapeCraftState.cpp
  Geoscape/GeoscapeState.cpp
  Geoscape/Globe.cpp
  Geoscape/GraphsState.cpp
  Geoscape/InterceptState.cpp
  Geoscape/ItemsArrivingState.cpp
  Geoscape/LowFuelState.cpp
  Geoscape/MissionDetectedState.cpp
  Geoscape/MonthlyReportState.cpp
  Geoscape/MultipleTargetsState.cpp
  Geoscape/NewPossibleManufactureState.cpp
  Geoscape/NewPossibleResearchState.cpp
  Geoscape/ProductionCompleteState.cpp
  Geoscape/PsiTrainingState.cpp
  Geoscape/ResearchCompleteState.cpp
  Geoscape/ResearchRequiredState.cpp
  Geoscape/SelectDestinationState.cpp
  Geoscape/TargetInfoState.cpp
  Geoscape/UfoDetectedState.cpp
  Geoscape/UfoLostState.cpp
)

set ( interface_src
  Interface/ArrowButton.cpp
  Interface/Bar.cpp
  Interface/BattlescapeButton.cpp
  Interface/ComboBox.cpp
  Interface/Cursor.cpp
  Interface/FpsCounter.cpp
  Interface/Frame.cpp
  Interface/ImageButton.cpp
  Interface/NumberText.cpp
  Interface/ScrollBar.cpp
  Interface/Slider.cpp
  Interface/Text.cpp
  Interface/TextButton.cpp
  Interface/TextEdit.cpp
  Interface/TextList.cpp
  Interface/ToggleTextButton.cpp
  Interface/Window.cpp
)

set ( menu_src
  Menu/AbandonGameState.cpp
  Menu/ConfirmLoadState.cpp
  Menu/CutsceneState.cpp
  Menu/DeleteGameState.cpp
  Menu/ErrorMessageState.cpp
  Menu/ListGamesState.cpp
  Menu/ListLoadOriginalState.cpp
  Menu/ListLoadState.cpp
  Menu/ListSaveState.cpp
  Menu/LoadGameState.cpp
  Menu/MainMenuState.cpp
  Menu/NewBattleState.cpp
  Menu/NewGameState.cpp
  Menu/OptionsAdvancedState.cpp
  Menu/OptionsAudioState.cpp
  Menu/OptionsBaseState.cpp
  Menu/OptionsBattlescapeState.cpp
  Menu/OptionsConfirmState.cpp
  Menu/OptionsControlsState.cpp
  Menu/OptionsDefaultsState.cpp
  Menu/OptionsFoldersState.cpp
  Menu/OptionsGeoscapeState.cpp
  Menu/ModConfirmExtendedState.cpp
  Menu/ModListState.cpp
  Menu/OptionsNoAudioState.cpp
  Menu/OptionsVideoState.cpp
  Menu/PauseState.cpp
  Menu/SaveGameState.cpp
  Menu/SetWindowedRootState.cpp
  Menu/SlideshowState.cpp
  Menu/StartState.cpp
  Menu/StatisticsState.cpp
  Menu/TestState.cpp
  Menu/VideoState.cpp
)

set ( mod_src
  Mod/AlienDeployment.cpp
  Mod/AlienRace.cpp
  Mod/Armor.cpp
  Mod/ArticleDefinition.cpp
  Mod/City.cpp
  Mod/ExtraSounds.cpp
  Mod/ExtraSprites.cpp
  Mod/ExtraStrings.cpp
  Mod/MCDPatch.cpp
  Mod/MapBlock.cpp
  Mod/MapData.cpp
  Mod/MapDataSet.cpp
  Mod/MapScript.cpp
  Mod/Mod.cpp
  Mod/Polygon.cpp
  Mod/Polyline.cpp
  Mod/RuleAlienMission.cpp
  Mod/RuleBaseFacility.cpp
  Mod/RuleCommendations.cpp
  Mod/RuleConverter.cpp
  Mod/RuleCountry.cpp
  Mod/RuleCraft.cpp
  Mod/RuleCraftWeapon.cpp
  Mod/RuleGlobe.cpp
  Mod/RuleInterface.cpp
  Mod/RuleInventory.cpp
  Mod/RuleItem.cpp
  Mod/RuleManufacture.cpp
  Mod/RuleMissionScript.cpp
  Mod/RuleMusic.cpp
  Mod/RuleRegion.cpp
  Mod/RuleResearch.cpp
  Mod/RuleSoldier.cpp
  Mod/RuleTerrain.cpp
  Mod/RuleUfo.cpp
  Mod/RuleVideo.cpp
  Mod/SoldierNamePool.cpp
  Mod/SoundDefinition.cpp
  Mod/StatString.cpp
  Mod/StatStringCondition.cpp
  Mod/Texture.cpp
  Mod/UfoTrajectory.cpp
  Mod/Unit.cpp
)

set ( savegame_src
  Savegame/AlienBase.cpp
  Savegame/AlienMission.cpp
  Savegame/AlienStrategy.cpp
  Savegame/Base.cpp
  Savegame/BaseFacility.cpp
  Savegame/BattleItem.cpp
  Savegame/BattleUnit.cpp
  Savegame/Country.cpp
  Savegame/Craft.cpp
  Savegame/CraftWeapon.cpp
  Savegame/CraftWeaponProjectile.cpp
  Savegame/EquipmentLayoutItem.cpp
  Savegame/GameTime.cpp
  Savegame/ItemContainer.cpp
  Savegame/MissionSite.cpp
  Savegame/MovingTarget.cpp
  Savegame/Node.cpp
  Savegame/Production.cpp
  Savegame/Region.cpp
  Savegame/ResearchProject.cpp
  Savegame/SaveConverter.cpp
  Savegame/SavedBattleGame.cpp
  Savegame/SavedGame.cpp
  Savegame/SerializationHelper.cpp
  Savegame/Soldier.cpp
  Savegame/SoldierDeath.cpp
  Savegame/SoldierDiary.cpp
  Savegame/Target.cpp
  Savegame/Tile.cpp
  Savegame/Transfer.cpp
  Savegame/Ufo.cpp
  Savegame/Vehicle.cpp
  Savegame/Waypoint.cpp
  Savegame/WeightedOptions.cpp
)

set ( ufopedia_src
  Ufopaedia/ArticleState.cpp
  Ufopaedia/ArticleStateArmor.cpp
  Ufopaedia/ArticleStateBaseFacility.cpp
  Ufopaedia/ArticleStateCraft.cpp
  Ufopaedia/ArticleStateCraftWeapon.cpp
  Ufopaedia/ArticleStateItem.cpp
  Ufopaedia/ArticleStateTFTD.cpp
  Ufopaedia/ArticleStateTFTDArmor.cpp
  Ufopaedia/ArticleStateTFTDCraft.cpp
  Ufopaedia/ArticleStateTFTDCraftWeapon.cpp
  Ufopaedia/ArticleStateTFTDFacility.cpp
  Ufopaedia/ArticleStateTFTDItem.cpp
  Ufopaedia/ArticleStateTFTDUso.cpp
  Ufopaedia/ArticleStateTFTDVehicle.cpp
  Ufopaedia/ArticleStateText.cpp
  Ufopaedia/ArticleStateTextImage.cpp
  Ufopaedia/ArticleStateUfo.cpp
  Ufopaedia/ArticleStateVehicle.cpp
  Ufopaedia/Ufopaedia.cpp
  Ufopaedia/UfopaediaSelectState.cpp
  Ufopaedia/UfopaediaStartState.cpp
)

# NOTE: to build with clang *and* libc++ on Linux, you need to call cmake like so:
# cmake -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++ \
#     -D CMAKE_CXX_FLAGS=-stdlib=libc++ -D CMAKE_EXE_LINKER_FLAGS=-stdlib=libc++
#     -D CMAKE_BUILD_TYPE=Release ...
# so both compiler and linker need the flag, otherwise linking breaks or uses libstdc++
# This requires yaml-cpp to also be linked to libc++ instead of libstdc++

set ( data_install_dir bin )
if ( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" AND "${CMAKE_BUILD_TYPE}" STREQUAL "Debug" )
  add_definitions ( -D_DEBUG )
endif ()
if ( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang" )
  if ( "${CMAKE_BUILD_TYPE}" STREQUAL "Debug" OR ENABLE_WARNING )
    # Enable more GCC warnings if requested or we are doing a Debug build.
    add_compile_options ( -Wall
      -Winit-self
      -Wmissing-include-dirs
      -Wunknown-pragmas
      -Wmissing-format-attribute
      -Wredundant-decls
      -Wformat-security
      -Wtype-limits
      -Wempty-body
      -Wuninitialized
      -Wignored-qualifiers
# These warning options actually generate way too many warnings. Should be enabled later.
#      -Weffc++
#      -Winline
#      -Wshadow
      )
    if ( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" )
      # add warning flags recognized by g++ but not by clang
      add_compile_options (
	  )
    elseif ( CMAKE_CXX_COMPILER_ID MATCHES "Clang" )
      # add warning flags recognized by clang but not by g++
    endif ()
    if ( FATAL_WARNING )
      add_compile_options ( -Werror )
    endif ()
  endif ()

# These warnings aren't worth polluting the code for
  add_compile_options (
    -Wno-unused-function              # cross-platform functions
    )
  if ( "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" )
    # add warning flags recognized by g++ but not by clang
    add_compile_options (
      )
  elseif ( CMAKE_CXX_COMPILER_ID MATCHES "Clang" )
    # add warning flags recognized by clang but not by g++
    add_compile_options (
      )
  endif ()
endif ()

if ( CMAKE_CXX_COMPILER_ID MATCHES "Clang" )
  if ( ENABLE_CLANG_ANALYSIS )
    add_compile_options ( --analyze )
  endif ()
endif ()

if ( MSVC )
  if ( ENABLE_WARNING )
    add_compile_options ( /W${MSVC_WARNING_LEVEL} )
  else ()
    # Set warning levels for Debug and Release with debug info
    set ( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /W${MSVC_WARNING_LEVEL}" )
    set ( CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /W${MSVC_WARNING_LEVEL}" )
  endif ()
  if ( FATAL_WARNING )
    add_compile_options ( /WX )
  endif ( FATAL_WARNING )
  set ( CMAKE_EXE_LINKER_FLAGS "/manifest:no" )
  set ( CMAKE_MODULE_LINKER_FLAGS "/manifest:no" ) 
  set ( CMAKE_SHARED_LINKER_FLAGS="/manifest:no" )
endif ()

if ( WIN32 )
  set ( application_type WIN32 )
  set ( openxcom_icon OpenXcom.rc )
endif ()

set ( openxcom_src ${root_src} ${basescape_src} ${battlescape_src} ${engine_src} ${geoscape_src} ${interface_src} ${menu_src} ${mod_src} ${savegame_src} ${ufopedia_src} )

set ( install_dest RUNTIME )
set ( set_exec_path ON )
set ( install_dest_dir bin )
if ( NOT "${DATADIR}" STREQUAL "" )
  add_definitions( -DDATADIR="${DATADIR}" )
endif ()
if ( UNIX AND NOT APPLE )
  set ( data_install_dir "${CMAKE_INSTALL_FULL_DATADIR}/openxcom" )
  add_definitions( -DINSTALLDIR="${data_install_dir}/" )
endif ()
if ( APPLE )
  set ( sdl_src "${MACOS_SDLMAIN_M_PATH}" )
  set ( openxcom_src ${openxcom_src} ${sdl_src} )
  set ( openxcom_icon "${PROJECT_SOURCE_DIR}/res/mac/AppIcon.icns" )
  if ( CREATE_BUNDLE )
    set ( application_type MACOSX_BUNDLE )
    set_source_files_properties( ${openxcom_icon} PROPERTIES MACOSX_PACKAGE_LOCATION Resources )
    set ( MACOSX_BUNDLE_ICON_FILE "AppIcon.icns" )
    set ( MACOSX_BUNDLE_INFO_STRING ${CPACK_PACKAGE_VENDOR} )
    set ( MACOSX_BUNDLE_GUI_IDENTIFIER "SupSuper.openxcom" )
    set ( MACOSX_BUNDLE_LONG_VERSION_STRING ${CPACK_PACKAGE_VERSION} )
    set ( MACOSX_BUNDLE_SHORT_VERSION_STRING "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}" )
    set ( MACOSX_BUNDLE_COPYRIGHT "Copyright © 2018 OpenXcom Developers. All rights reserved." )
    set ( EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR} )
    set ( install_dest BUNDLE )
    set ( set_exec_path OFF )
    set ( install_dest_dir "" )
    set ( data_install_dir bin )
    if ( "${CMAKE_GENERATOR}" STREQUAL "Xcode" )
      set ( bundle_path "${EXECUTABLE_OUTPUT_PATH}/${CMAKE_BUILD_TYPE}/openxcom.app" )
    else ()
      set ( bundle_path "${EXECUTABLE_OUTPUT_PATH}/openxcom.app" )
    endif ()
  else ()
    set ( data_install_dir "${CMAKE_INSTALL_FULL_DATADIR}/openxcom" )
    add_definitions( -DINSTALLDIR="${data_install_dir}/" )
  endif ()
endif ()
if ( set_exec_path )
  set ( EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin )
endif ()

if ( FORCE_INSTALL_DATA_TO_BIN )
  set ( data_install_dir bin )
endif ()

# Full engine sources for companion_snapshot_terrain_test (copy of openxcom_src without the game entrypoint).
set ( companion_snapshot_terrain_test_src ${openxcom_src} )
list ( REMOVE_ITEM companion_snapshot_terrain_test_src main.cpp )

add_executable ( openxcom  ${application_type} ${openxcom_src} ${openxcom_icon} )
install ( TARGETS openxcom ${install_dest} DESTINATION ${CMAKE_INSTALL_BINDIR} )
# Extra link flags for Windows. They need to be set before the SDL/YAML link flags, otherwise you will get strange link errors ('Undefined reference to WinMain@16')
if ( WIN32 )
  set ( basic_windows_libs advapi32.lib shell32.lib shlwapi.lib bcrypt.lib )
  if ( MINGW )
    set ( basic_windows_libs ${basic_windows_libs} mingw32 -mwindows )
    set ( static_flags  -static )
    set ( SDLMIXER_LIBRARY "${SDLMIXER_LIBRARY} -lwinmm" )
  endif ()
  set ( system_libs ${basic_windows_libs} SDLmain ${static_flags} )
endif ()

# backtrace(3) requires libexecinfo on some *BSD systems
if (${CMAKE_SYSTEM_NAME} MATCHES FreeBSD OR ${CMAKE_SYSTEM_NAME} MATCHES NetBSD OR ${CMAKE_SYSTEM_NAME} MATCHES OpenBSD)
  set ( system_libs -lexecinfo )
endif ()

target_link_libraries ( openxcom ${system_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )

option ( OPENXCOM_BUILD_COMPANION_EVENT_LOG_TESTS "Build companion event log regression tests" ON )
if ( WIN32 )
  function ( openxcom_stage_win32_runtime_deps target_name )
    if ( CMAKE_CL_64 )
      file ( GLOB _oxc_stage_dlls "${DEPS_DIR}/lib/x64/*.dll" )
    else ()
      file ( GLOB _oxc_stage_dlls "${DEPS_DIR}/lib/Win32/*.dll" )
    endif ()
    foreach ( _dll ${_oxc_stage_dlls} )
      add_custom_command ( TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_dll} $<TARGET_FILE_DIR:${target_name}>
        VERBATIM )
    endforeach ()
  endfunction ()
endif ()
if ( OPENXCOM_BUILD_COMPANION_EVENT_LOG_TESTS )
  add_executable ( companion_event_log_test
    test/companion_event_log_test.cpp
    Engine/CompanionEventLogCore.cpp
    Engine/RNG.cpp
  )
  target_include_directories ( companion_event_log_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_event_log_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_event_log_test ${basic_windows_libs} )
  endif ()
  add_test ( NAME companion_event_log_test COMMAND companion_event_log_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_PERCEPTION_LOG_TESTS "Build companion perception log regression tests (M1-WP6)" ON )
if ( OPENXCOM_BUILD_COMPANION_PERCEPTION_LOG_TESTS )
  add_executable ( companion_perception_log_test
    test/companion_perception_log_test.cpp
    Engine/CompanionPerceptionLogCore.cpp
    Engine/CompanionSnapshotUnitStateCore.cpp
    Engine/RNG.cpp
  )
  target_include_directories ( companion_perception_log_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_perception_log_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_perception_log_test ${basic_windows_libs} )
  endif ()
  add_test ( NAME companion_perception_log_test COMMAND companion_perception_log_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_UNIFORM_SHOOTING_SPREAD_TESTS "Build uniform shooting spread regression tests (M1)" ON )
if ( OPENXCOM_BUILD_COMPANION_UNIFORM_SHOOTING_SPREAD_TESTS )
  add_executable ( companion_uniform_shooting_spread_test
    test/companion_uniform_shooting_spread_test.cpp
    Engine/CompanionUniformShootingSpreadCore.cpp
    Engine/RNG.cpp
  )
  target_include_directories ( companion_uniform_shooting_spread_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_uniform_shooting_spread_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_uniform_shooting_spread_test ${basic_windows_libs} )
  endif ()
  add_test ( NAME companion_uniform_shooting_spread_test COMMAND companion_uniform_shooting_spread_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_GROUND_ITEM_TESTS "Build companion ground-item observation tests (M1-WP9)" ON )
if ( OPENXCOM_BUILD_COMPANION_GROUND_ITEM_TESTS )
  add_executable ( companion_ground_item_test
    test/companion_ground_item_test.cpp
    Engine/CompanionGroundItemObservationCore.cpp
  )
  target_include_directories ( companion_ground_item_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_ground_item_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_ground_item_test ${basic_windows_libs} )
  endif ()
  add_test ( NAME companion_ground_item_test COMMAND companion_ground_item_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_BRIEFING_REQUEST_TESTS "Build companion briefing request regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_BRIEFING_REQUEST_TESTS )
  add_executable ( companion_briefing_request_test
    test/companion_briefing_request_test.cpp
    Engine/CompanionBriefingRequestCore.cpp
    Engine/CompanionSha256.cpp
    Engine/RNG.cpp
  )
  target_include_directories ( companion_briefing_request_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_briefing_request_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_briefing_request_test ${basic_windows_libs} )
  endif ()
  add_test ( NAME companion_briefing_request_test COMMAND companion_briefing_request_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_COMMAND_TESTS "Build companion tactical command regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_COMMAND_TESTS )
  add_executable ( companion_command_test
    test/companion_command_test.cpp
    Engine/CompanionCommandCore.cpp
    Engine/CompanionSoldierOwnershipCore.cpp
    Engine/CompanionJsonLite.cpp
    Engine/CompanionCommandRuntimeSupport.cpp
    Engine/CompanionCommandCasualtySettlementCore.cpp
    Engine/CompanionCommandJournalCore.cpp
    Engine/CompanionSha256.cpp
    Engine/RNG.cpp
  )
  target_include_directories ( companion_command_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_command_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_command_test ${basic_windows_libs} )
  endif ()
  add_test ( NAME companion_command_test COMMAND companion_command_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_INVENTORY_MOVE_ENGINE_TEST "Build companion INVENTORY_MOVE engine integration test (WP8B3)" ON )
if ( OPENXCOM_BUILD_COMPANION_INVENTORY_MOVE_ENGINE_TEST )
  add_executable ( companion_inventory_move_engine_test
    test/companion_inventory_move_engine_test.cpp
    ${companion_snapshot_terrain_test_src}
  )
  target_include_directories ( companion_inventory_move_engine_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_inventory_move_engine_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_inventory_move_engine_test ${basic_windows_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
    openxcom_stage_win32_runtime_deps ( companion_inventory_move_engine_test )
  else ()
    target_link_libraries ( companion_inventory_move_engine_test ${system_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_inventory_move_engine_test COMMAND companion_inventory_move_engine_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_DIRECTED_FIRE_PREFLIGHT_TEST "Build companion directed-fire preflight regression test (P1-FIRE-LANE-03)" ON )
if ( OPENXCOM_BUILD_COMPANION_DIRECTED_FIRE_PREFLIGHT_TEST )
  add_executable ( companion_directed_fire_preflight_test
    test/companion_directed_fire_preflight_test.cpp
    ${companion_snapshot_terrain_test_src}
  )
  target_include_directories ( companion_directed_fire_preflight_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_directed_fire_preflight_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_directed_fire_preflight_test ${basic_windows_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
    openxcom_stage_win32_runtime_deps ( companion_directed_fire_preflight_test )
  else ()
    target_link_libraries ( companion_directed_fire_preflight_test ${system_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_directed_fire_preflight_test COMMAND companion_directed_fire_preflight_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_MOVE_PLAYER_ORDERED_ENGINE_TEST "Build companion MOVE playerOrdered regression test (P1-MOVE-INTERRUPT-02A)" ON )
if ( OPENXCOM_BUILD_COMPANION_MOVE_PLAYER_ORDERED_ENGINE_TEST )
  add_executable ( companion_move_player_ordered_engine_test
    test/companion_move_player_ordered_engine_test.cpp
    ${companion_snapshot_terrain_test_src}
  )
  target_include_directories ( companion_move_player_ordered_engine_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_move_player_ordered_engine_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_move_player_ordered_engine_test ${basic_windows_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
    openxcom_stage_win32_runtime_deps ( companion_move_player_ordered_engine_test )
  else ()
    target_link_libraries ( companion_move_player_ordered_engine_test ${system_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_move_player_ordered_engine_test COMMAND companion_move_player_ordered_engine_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_PICKUP_DROP_ENGINE_TEST "Build companion PICKUP/DROP engine integration test (WP8B4V)" ON )
if ( OPENXCOM_BUILD_COMPANION_PICKUP_DROP_ENGINE_TEST )
  add_executable ( companion_pickup_drop_engine_test
    test/companion_pickup_drop_engine_test.cpp
    ${companion_snapshot_terrain_test_src}
  )
  target_include_directories ( companion_pickup_drop_engine_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_pickup_drop_engine_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_pickup_drop_engine_test ${basic_windows_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
    openxcom_stage_win32_runtime_deps ( companion_pickup_drop_engine_test )
  else ()
    target_link_libraries ( companion_pickup_drop_engine_test ${system_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_pickup_drop_engine_test COMMAND companion_pickup_drop_engine_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_COMMAND_JOURNAL_TESTS "Build companion command journal regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_COMMAND_JOURNAL_TESTS )
  add_executable ( companion_command_journal_test
    test/companion_command_journal_test.cpp
    Engine/CompanionCommandJournalCore.cpp
    Engine/CompanionCommandCore.cpp
    Engine/CompanionJsonLite.cpp
    Engine/CompanionSha256.cpp
    Engine/RNG.cpp
  )
  target_include_directories ( companion_command_journal_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_command_journal_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_command_journal_test ${basic_windows_libs} )
  endif ()
  add_test ( NAME companion_command_journal_test COMMAND companion_command_journal_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_TACTICAL_HOVER_COORDS_TESTS "Build companion tactical hover coordinate regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_TACTICAL_HOVER_COORDS_TESTS )
  add_executable ( companion_tactical_hover_coords_test
    test/companion_tactical_hover_coords_test.cpp
    Engine/CompanionTacticalHoverCoordsCore.cpp
  )
  target_include_directories ( companion_tactical_hover_coords_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_tactical_hover_coords_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_tactical_hover_coords_test ${basic_windows_libs} )
  endif ()
  add_test ( NAME companion_tactical_hover_coords_test COMMAND companion_tactical_hover_coords_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_GITHUB_QUOTA_HUD_TESTS "Build companion GitHub quota HUD regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_GITHUB_QUOTA_HUD_TESTS )
  add_executable ( companion_github_quota_hud_test
    test/companion_github_quota_hud_test.cpp
    Engine/CompanionGithubQuotaHudCore.cpp
    Engine/CompanionBriefingRequestCore.cpp
    Engine/CompanionJsonLite.cpp
    Engine/CompanionSha256.cpp
  )
  target_include_directories ( companion_github_quota_hud_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_github_quota_hud_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_github_quota_hud_test ${basic_windows_libs} )
  endif ()
  add_test ( NAME companion_github_quota_hud_test COMMAND companion_github_quota_hud_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_ON_DEMAND_RELAY_TESTS "Build companion on-demand relay regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_ON_DEMAND_RELAY_TESTS )
  add_executable ( companion_on_demand_relay_test
    test/companion_on_demand_relay_test.cpp
    Engine/CompanionAutoRetrieveCore.cpp
    Engine/CompanionOnDemandRelayCore.cpp
    Engine/CompanionStrategicSendEnqueueSupport.cpp
    Engine/CompanionOnDemandRelayLayout.cpp
    Engine/CompanionRelayPublicationFreshnessCore.cpp
    Engine/CompanionBriefingRequestCore.cpp
    Engine/CompanionSha256.cpp
    Engine/CompanionJsonLite.cpp
  )
  target_compile_definitions ( companion_on_demand_relay_test PRIVATE COMPANION_RELAY_TEST_STUB_STRATEGIC_EXPORT )
  target_include_directories ( companion_on_demand_relay_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_on_demand_relay_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_on_demand_relay_test ${basic_windows_libs} )
  else ()
    target_link_libraries ( companion_on_demand_relay_test ${system_libs} )
  endif ()
  add_test ( NAME companion_on_demand_relay_test COMMAND companion_on_demand_relay_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_JSON_LITE_TESTS "Build companion JSON lite regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_JSON_LITE_TESTS )
  add_executable ( companion_json_lite_test
    test/companion_json_lite_test.cpp
    Engine/CompanionJsonLite.cpp
  )
  target_include_directories ( companion_json_lite_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_json_lite_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_json_lite_test ${basic_windows_libs} )
  else ()
    target_link_libraries ( companion_json_lite_test ${system_libs} )
  endif ()
  add_test ( NAME companion_json_lite_test COMMAND companion_json_lite_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_CAMPAIGN_IDENTITY_TESTS "Build companion campaign identity regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_CAMPAIGN_IDENTITY_TESTS )
  add_executable ( companion_campaign_identity_test
    test/companion_campaign_identity_test.cpp
    Engine/CompanionCampaignIdentityCore.cpp
  )
  target_include_directories ( companion_campaign_identity_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_campaign_identity_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_campaign_identity_test ${basic_windows_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_campaign_identity_test ${system_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_campaign_identity_test COMMAND companion_campaign_identity_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_CAMPAIGN_OWNERSHIP_TESTS "Build companion campaign ownership regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_CAMPAIGN_OWNERSHIP_TESTS )
  add_executable ( companion_campaign_ownership_test
    test/companion_campaign_ownership_test.cpp
    Engine/CompanionCampaignIdentityCore.cpp
    Engine/CompanionCampaignOwnershipCore.cpp
  )
  target_include_directories ( companion_campaign_ownership_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_campaign_ownership_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_campaign_ownership_test ${basic_windows_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_campaign_ownership_test ${system_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_campaign_ownership_test COMMAND companion_campaign_ownership_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_TACTICAL_AUTHORITY_TESTS "Build companion tactical authority regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_TACTICAL_AUTHORITY_TESTS )
  add_executable ( companion_tactical_authority_test
    test/companion_tactical_authority_test.cpp
    Engine/CompanionTacticalAuthorityCore.cpp
    Engine/CompanionCampaignOwnershipCore.cpp
    Engine/CompanionSoldierOwnershipCore.cpp
    Engine/CompanionCommandCore.cpp
    Engine/CompanionJsonLite.cpp
    Engine/CompanionSha256.cpp
  )
  target_include_directories ( companion_tactical_authority_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_tactical_authority_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_tactical_authority_test ${basic_windows_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_tactical_authority_test ${system_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_tactical_authority_test COMMAND companion_tactical_authority_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_SOLDIER_CONTROL_PUBLISH_TESTS "Build companion soldier control publish regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_SOLDIER_CONTROL_PUBLISH_TESTS )
  add_executable ( companion_soldier_control_publish_test
    test/companion_soldier_control_publish_test.cpp
    Engine/CompanionSoldierControlPublishCore.cpp
    Engine/CompanionCampaignOwnershipCore.cpp
    Engine/CompanionCampaignIdentityCore.cpp
    Engine/CompanionSoldierOwnershipCore.cpp
    Engine/CompanionCommandCore.cpp
    Engine/CompanionJsonLite.cpp
    Engine/CompanionSha256.cpp
    Engine/CompanionSnapshotExportCore.cpp
  )
  target_include_directories ( companion_soldier_control_publish_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_soldier_control_publish_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_soldier_control_publish_test ${basic_windows_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_soldier_control_publish_test ${system_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_soldier_control_publish_test COMMAND companion_soldier_control_publish_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_TACTICAL_OWNERSHIP_TESTS "Build companion tactical ownership regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_TACTICAL_OWNERSHIP_TESTS )
  add_executable ( companion_tactical_ownership_test
    test/companion_tactical_ownership_test.cpp
    Engine/CompanionTacticalOwnershipCore.cpp
    Engine/CompanionTacticalAuthorityCore.cpp
    Engine/CompanionCampaignOwnershipCore.cpp
    Engine/CompanionCampaignIdentityCore.cpp
    Engine/CompanionSoldierOwnershipCore.cpp
    Engine/CompanionCommandCore.cpp
    Engine/CompanionJsonLite.cpp
    Engine/CompanionSha256.cpp
  )
  target_include_directories ( companion_tactical_ownership_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_tactical_ownership_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_tactical_ownership_test ${basic_windows_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_tactical_ownership_test ${system_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_tactical_ownership_test COMMAND companion_tactical_ownership_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_SOLDIER_ASSIGNMENT_UI_TESTS "Build companion soldier assignment UI regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_SOLDIER_ASSIGNMENT_UI_TESTS )
  add_executable ( companion_soldier_assignment_ui_test
    test/companion_soldier_assignment_ui_test.cpp
    Engine/CompanionCampaignOwnershipCore.cpp
  )
  target_include_directories ( companion_soldier_assignment_ui_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_soldier_assignment_ui_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_soldier_assignment_ui_test ${basic_windows_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_soldier_assignment_ui_test ${system_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_soldier_assignment_ui_test COMMAND companion_soldier_assignment_ui_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_BATTLESCAPE_OWNERSHIP_TESTS "Build companion battlescape ownership regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_BATTLESCAPE_OWNERSHIP_TESTS )
  add_executable ( companion_battlescape_ownership_test
    test/companion_battlescape_ownership_test.cpp
    Engine/CompanionOwnershipHandoffCore.cpp
    Engine/CompanionCampaignOwnershipCore.cpp
  )
  target_include_directories ( companion_battlescape_ownership_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_battlescape_ownership_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_battlescape_ownership_test ${basic_windows_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_battlescape_ownership_test ${system_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_battlescape_ownership_test COMMAND companion_battlescape_ownership_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_SNAPSHOT_TERRAIN_TESTS "Build companion snapshot terrain regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_SNAPSHOT_TERRAIN_TESTS )
  add_executable ( companion_snapshot_terrain_test
    test/companion_snapshot_terrain_test.cpp
    ${companion_snapshot_terrain_test_src}
  )
  target_include_directories ( companion_snapshot_terrain_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_snapshot_terrain_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_snapshot_terrain_test ${basic_windows_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_snapshot_terrain_test ${system_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_snapshot_terrain_test COMMAND companion_snapshot_terrain_test )
  if ( WIN32 )
    openxcom_stage_win32_runtime_deps ( companion_snapshot_terrain_test )
  endif ()
endif ()

option ( OPENXCOM_BUILD_COMPANION_SNAPSHOT_EXPORT_TESTS "Build companion snapshot export fallback regression tests" ON )
if ( OPENXCOM_BUILD_COMPANION_SNAPSHOT_EXPORT_TESTS )
  add_executable ( companion_snapshot_export_test
    test/companion_snapshot_export_test.cpp
    Engine/CompanionSnapshotExportCore.cpp
    Engine/CompanionSha256.cpp
  )
  add_executable ( companion_manual_v5_export_test
    test/companion_manual_v5_export_test.cpp
    Engine/CompanionManualV5Export.cpp
    Engine/CompanionManualExportRetention.cpp
    Engine/CompanionSnapshotExportCore.cpp
    Engine/CompanionBriefingRequestCore.cpp
    Engine/CompanionJsonLite.cpp
    Engine/CompanionSha256.cpp
  )
  target_include_directories ( companion_snapshot_export_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_snapshot_export_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_snapshot_export_test ${basic_windows_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_snapshot_export_test ${system_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_snapshot_export_test COMMAND companion_snapshot_export_test )
  if ( WIN32 )
    openxcom_stage_win32_runtime_deps ( companion_snapshot_export_test )
  endif ()
  target_include_directories ( companion_manual_v5_export_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_manual_v5_export_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_manual_v5_export_test ${basic_windows_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_manual_v5_export_test ${system_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_manual_v5_export_test COMMAND companion_manual_v5_export_test )
  if ( WIN32 )
    openxcom_stage_win32_runtime_deps ( companion_manual_v5_export_test )
  endif ()
  add_executable ( companion_manual_upload_hud_test
    test/companion_manual_upload_hud_test.cpp
    Engine/CompanionManualUploadHudCore.cpp
    Engine/CompanionManualV5Export.cpp
    Engine/CompanionManualExportRetention.cpp
    Engine/CompanionOnDemandRelayLayout.cpp
    Engine/CompanionOnDemandRelayCore.cpp
    Engine/CompanionSnapshotExportCore.cpp
    Engine/CompanionJsonLite.cpp
    Engine/CompanionBriefingRequestCore.cpp
    Engine/CompanionSha256.cpp
  )
  target_include_directories ( companion_manual_upload_hud_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_manual_upload_hud_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_manual_upload_hud_test ${basic_windows_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_manual_upload_hud_test ${system_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_manual_upload_hud_test COMMAND companion_manual_upload_hud_test )
  if ( WIN32 )
    openxcom_stage_win32_runtime_deps ( companion_manual_upload_hud_test )
  endif ()
  add_executable ( companion_manual_export_retention_test
    test/companion_manual_export_retention_test.cpp
    Engine/CompanionManualExportRetention.cpp
    Engine/CompanionManualV5Export.cpp
    Engine/CompanionSnapshotExportCore.cpp
    Engine/CompanionJsonLite.cpp
    Engine/CompanionBriefingRequestCore.cpp
    Engine/CompanionSha256.cpp
  )
  target_include_directories ( companion_manual_export_retention_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_manual_export_retention_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_manual_export_retention_test ${basic_windows_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_manual_export_retention_test ${system_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_manual_export_retention_test COMMAND companion_manual_export_retention_test )
  if ( WIN32 )
    openxcom_stage_win32_runtime_deps ( companion_manual_export_retention_test )
  endif ()
  add_executable ( companion_manual_upload_integration_test
    test/companion_manual_upload_integration_test.cpp
    Engine/CompanionManualUploadHudCore.cpp
    Engine/CompanionManualV5Export.cpp
    Engine/CompanionManualExportRetention.cpp
    Engine/CompanionSnapshotExportCore.cpp
    Engine/CompanionJsonLite.cpp
    Engine/CompanionBriefingRequestCore.cpp
    Engine/CompanionSha256.cpp
  )
  target_include_directories ( companion_manual_upload_integration_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_manual_upload_integration_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_manual_upload_integration_test ${basic_windows_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_manual_upload_integration_test ${system_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_manual_upload_integration_test COMMAND companion_manual_upload_integration_test )
  if ( WIN32 )
    openxcom_stage_win32_runtime_deps ( companion_manual_upload_integration_test )
  endif ()
endif ()

option ( OPENXCOM_BUILD_COMPANION_SNAPSHOT_UNIT_STATE_TESTS "Build companion snapshot unit state tests (M1-WP2)" ON )
if ( OPENXCOM_BUILD_COMPANION_SNAPSHOT_UNIT_STATE_TESTS )
  add_executable ( companion_snapshot_unit_state_test
    test/companion_snapshot_unit_state_test.cpp
    Engine/CompanionSnapshotUnitStateCore.cpp
  )
  target_include_directories ( companion_snapshot_unit_state_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_snapshot_unit_state_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_snapshot_unit_state_test ${basic_windows_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_snapshot_unit_state_test ${system_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_snapshot_unit_state_test COMMAND companion_snapshot_unit_state_test )
  if ( WIN32 )
    openxcom_stage_win32_runtime_deps ( companion_snapshot_unit_state_test )
  endif ()
endif ()

option ( OPENXCOM_BUILD_COMPANION_SNAPSHOT_SERIALIZATION_TESTS "Build companion snapshot serialization integration tests (M1-WP2B)" ON )
if ( OPENXCOM_BUILD_COMPANION_SNAPSHOT_SERIALIZATION_TESTS )
  add_executable ( companion_snapshot_serialization_test
    test/companion_snapshot_serialization_test.cpp
    ${companion_snapshot_terrain_test_src}
  )
  target_include_directories ( companion_snapshot_serialization_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_snapshot_serialization_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_snapshot_serialization_test ${basic_windows_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
    openxcom_stage_win32_runtime_deps ( companion_snapshot_serialization_test )
  else ()
    target_link_libraries ( companion_snapshot_serialization_test ${system_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_snapshot_serialization_test COMMAND companion_snapshot_serialization_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_RULES_REFERENCE_TESTS "Build companion rules reference export tests (M1-WP5)" ON )
if ( OPENXCOM_BUILD_COMPANION_RULES_REFERENCE_TESTS )
  add_executable ( companion_rules_reference_export_test
    test/companion_rules_reference_export_test.cpp
    Engine/CompanionRulesReferenceExportCore.cpp
    Engine/CompanionCampaignIdentityCore.cpp
    Engine/CompanionJsonLite.cpp
    Engine/CompanionSha256.cpp
  )
  target_include_directories ( companion_rules_reference_export_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_rules_reference_export_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_rules_reference_export_test ${basic_windows_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  else ()
    target_link_libraries ( companion_rules_reference_export_test ${system_libs} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_rules_reference_export_test COMMAND companion_rules_reference_export_test )
  if ( WIN32 )
    openxcom_stage_win32_runtime_deps ( companion_rules_reference_export_test )
  endif ()
endif ()

option ( OPENXCOM_BUILD_COMPANION_BASE_INTELLIGENCE_TESTS "Build companion base intelligence export tests (M1-BI1)" ON )
if ( OPENXCOM_BUILD_COMPANION_BASE_INTELLIGENCE_TESTS )
  # Full engine link (minus main.cpp) — same pattern as companion_snapshot_serialization_test.
  add_executable ( companion_base_intelligence_export_test
    test/companion_base_intelligence_export_test.cpp
    ${companion_snapshot_terrain_test_src}
  )
  target_include_directories ( companion_base_intelligence_export_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_base_intelligence_export_test PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_base_intelligence_export_test ${basic_windows_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
    openxcom_stage_win32_runtime_deps ( companion_base_intelligence_export_test )
  else ()
    target_link_libraries ( companion_base_intelligence_export_test ${system_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
  add_test ( NAME companion_base_intelligence_export_test COMMAND companion_base_intelligence_export_test )
endif ()

option ( OPENXCOM_BUILD_COMPANION_P5_FULL_REVEAL_STRESS_HARNESS "Build P4 step 5 direct battle generation stress harness" ON )
if ( OPENXCOM_BUILD_COMPANION_P5_FULL_REVEAL_STRESS_HARNESS )
  add_executable ( companion_p5_full_reveal_stress_harness
    test/companion_p5_full_reveal_stress_harness.cpp
    ${companion_snapshot_terrain_test_src}
  )
  target_include_directories ( companion_p5_full_reveal_stress_harness PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR} )
  if ( WIN32 )
    set_target_properties ( companion_p5_full_reveal_stress_harness PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE" )
    target_link_libraries ( companion_p5_full_reveal_stress_harness ${basic_windows_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
    openxcom_stage_win32_runtime_deps ( companion_p5_full_reveal_stress_harness )
  else ()
    target_link_libraries ( companion_p5_full_reveal_stress_harness ${system_libs} ${SDLIMAGE_LIBRARY} ${SDLMIXER_LIBRARY} ${SDLGFX_LIBRARY} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} ${EXTRA_XCOM_LIBS} debug ${YAMLCPP_LIBRARY_DEBUG} optimized ${YAMLCPP_LIBRARY} )
  endif ()
endif ()

# Pack libraries into bundle and link executable appropriately
if ( APPLE AND CREATE_BUNDLE )
  include ( PostprocessBundle )
  postprocess_bundle ( openxcom ${bundle_path} )
endif ()

set ( bin_data_dirs TFTD UFO common standard )
foreach ( binpath ${bin_data_dirs} )
  if ( APPLE AND CREATE_BUNDLE )
    add_custom_command ( TARGET openxcom
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/bin/${binpath} ${bundle_path}/Contents/Resources/${binpath}
	  VERBATIM )
  else ()
    add_custom_command ( TARGET openxcom
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/bin/${binpath} ${EXECUTABLE_OUTPUT_PATH}/${binpath}
	  VERBATIM )
  endif ()
  install ( DIRECTORY ${CMAKE_SOURCE_DIR}/bin/${binpath} DESTINATION ${data_install_dir} )
endforeach()

# Copy Windows DLLs to bin folder
if ( WIN32 )
  if ( CMAKE_CL_64 )
    file ( GLOB Windows_dll "${DEPS_DIR}/lib/x64/*.dll" )
  else ( )
    file ( GLOB Windows_dll "${DEPS_DIR}/lib/Win32/*.dll" )
  endif()
  foreach ( dll ${Windows_dll} )
    add_custom_command ( TARGET openxcom
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${dll} ${EXECUTABLE_OUTPUT_PATH}
	  VERBATIM )
    install ( FILES ${dll} DESTINATION bin)
  endforeach()
endif ()

#Setup source groups for IDE
if ( MSVC OR "${CMAKE_GENERATOR}" STREQUAL "Xcode" )
  source_group ( "Basescape" FILES ${basescape_src} )
  source_group ( "Battlescape" FILES ${battlescape_src} )
  source_group ( "Engine" FILES ${engine_src} )
  source_group ( "Geoscape" FILES ${geoscape_src} )
  source_group ( "Interface" FILES ${interface_src} )
  source_group ( "Menu" FILES ${menu_src} )
  source_group ( "Mod" FILES ${mod_src} )
  source_group ( "Savegame" FILES ${savegame_src} )
  source_group ( "Ufopedia" FILES ${ufopedia_src} )
  if ( APPLE )
    source_group ( "SDL" FILES ${sdl_src} )
  endif ()
endif ()
