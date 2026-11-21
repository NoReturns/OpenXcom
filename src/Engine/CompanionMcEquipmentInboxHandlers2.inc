	// MC_RELOAD / MC_UNLOAD / MC_INVENTORY_MOVE / MC_PICKUP / MC_DROP handlers (M1 MC-04).
	// Whitelisted types without a matching block would leave the inbox unprocessed.

	if (inboxCommandType == "MC_RELOAD")
	{
		if (!CompanionTacticalMcHostileEquipmentExecutionCore::mcEquipmentRelayExecutionSupported())
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			CompanionCommandCore::ReloadCommandParsed unsupported = {};
			unsupported.rejectCode = "MC_EXECUTION_NOT_SUPPORTED";
			unsupported.commandType = inboxCommandType;
			CompanionJsonLite::extractJsonStringField(commandJson, "command_id", &unsupported.commandId);
			rejectReloadCommand(resultPath, unsupported, "MC_EXECUTION_NOT_SUPPORTED",
				"MC hostile equipment relay execution is not enabled.", inboxDigest);
			logRejectOnce(unsupported.commandId.empty() ? "unknown" : unsupported.commandId,
				"MC_EXECUTION_NOT_SUPPORTED");
			return;
		}

		CompanionCommandCore::ReloadCommandParsed mcReloadParsed = {};
		if (!CompanionCommandCore::parseMcReloadCommandJson(commandJson, &mcReloadParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectReloadCommand(resultPath, mcReloadParsed, mcReloadParsed.rejectCode, mcReloadParsed.rejectCode, inboxDigest);
			logRejectOnce(mcReloadParsed.commandId.empty() ? "unknown" : mcReloadParsed.commandId, mcReloadParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(mcReloadParsed.commandId);
		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, 0, 0, 0, 0, &mcReloadParsed, 0, 0, 0, 0, 0))
		{
			logRejectOnce(mcReloadParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, mcReloadParsed.commandId);
		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, mcReloadParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, mcReloadParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, 0, 0, 0, 0, &mcReloadParsed, 0, 0, 0, 0, 0))
		{
			return;
		}

		if (g_active.inFlight && g_active.commandId != mcReloadParsed.commandId)
		{
			rejectReloadCommand(resultPath, mcReloadParsed, "PRIOR_COMMAND_NON_TERMINAL",
				"A prior command is still executing.", inboxDigest);
			appendJournalEntry(journalPath, mcReloadParsed.commandId);
			logRejectOnce(mcReloadParsed.commandId, "PRIOR_COMMAND_NON_TERMINAL");
			return;
		}

		BattleUnit *actor = findUnitById(battle, mcReloadParsed.actorUnitId);
		CompanionTacticalMcHostileEquipmentExecutionCore::McReloadValidationInput mcInput = {};
		fillMcRemoteAuthorityInput(
			&mcInput.authority, game, battle, campaign, battleInitialized, controlJson, controlReadOk, sessionId,
			mcReloadParsed, actor);
		mcInput.contract = CompanionTacticalMcHostileEquipmentExecutionCore::buildMcReloadContractInput(
			game, battle, campaign, mcReloadParsed, actor);
		const CompanionCommandCore::MoveValidationResult verdict =
			CompanionTacticalMcHostileEquipmentExecutionCore::validateMcReloadExecutionPreflight(mcInput);
		if (!verdict.accepted)
		{
			rejectReloadCommand(resultPath, mcReloadParsed, verdict.rejectCode, verdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, mcReloadParsed.commandId);
			logRejectOnce(mcReloadParsed.commandId, verdict.rejectCode);
			return;
		}

		logCommandDiagnostic("accepted MC_RELOAD command_id=" + mcReloadParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(mcReloadParsed.actorUnitId));

		BattleItem *weapon = findBattleItemByIdOnUnit(actor, mcReloadParsed.weaponBattleItemId);
		BattleItem *ammo = findBattleItemByIdOnUnit(actor, mcReloadParsed.ammoBattleItemId);
		const Position pos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		std::string rejectReason;
		if (!game->tryExecuteCompanionReload(actor, weapon, ammo, &rejectReason))
		{
			rejectReloadCommand(resultPath, mcReloadParsed, "RELOAD_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, mcReloadParsed.commandId);
			logRejectOnce(mcReloadParsed.commandId, "RELOAD_START_FAILED");
			return;
		}
		const std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_COMPLETED);
		writeResultFile(resultPath, buildReloadResultJson(mcReloadParsed, CompanionCommandCore::CMD_COMPLETED, std::string(), pos, actor != 0 ? actor->getTimeUnits() : 0, actor != 0 && actor->isKneeled(), resultSnapshotSha256, inboxDigest, 15));
		logTerminalOnce(mcReloadParsed.commandId, CompanionCommandCore::CMD_COMPLETED);
		appendJournalEntry(journalPath, mcReloadParsed.commandId);
		return;
	}
	if (inboxCommandType == "MC_UNLOAD")
	{
		if (!CompanionTacticalMcHostileEquipmentExecutionCore::mcEquipmentRelayExecutionSupported())
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			CompanionCommandCore::UnloadCommandParsed unsupported = {};
			unsupported.rejectCode = "MC_EXECUTION_NOT_SUPPORTED";
			unsupported.commandType = inboxCommandType;
			CompanionJsonLite::extractJsonStringField(commandJson, "command_id", &unsupported.commandId);
			rejectUnloadCommand(resultPath, unsupported, "MC_EXECUTION_NOT_SUPPORTED",
				"MC hostile equipment relay execution is not enabled.", inboxDigest);
			logRejectOnce(unsupported.commandId.empty() ? "unknown" : unsupported.commandId,
				"MC_EXECUTION_NOT_SUPPORTED");
			return;
		}

		CompanionCommandCore::UnloadCommandParsed mcUnloadParsed = {};
		if (!CompanionCommandCore::parseMcUnloadCommandJson(commandJson, &mcUnloadParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectUnloadCommand(resultPath, mcUnloadParsed, mcUnloadParsed.rejectCode, mcUnloadParsed.rejectCode, inboxDigest);
			logRejectOnce(mcUnloadParsed.commandId.empty() ? "unknown" : mcUnloadParsed.commandId, mcUnloadParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(mcUnloadParsed.commandId);
		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, 0, 0, 0, 0, 0, &mcUnloadParsed, 0, 0, 0, 0))
		{
			logRejectOnce(mcUnloadParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, mcUnloadParsed.commandId);
		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, mcUnloadParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, mcUnloadParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, 0, 0, 0, 0, 0, &mcUnloadParsed, 0, 0, 0, 0))
		{
			return;
		}

		if (g_active.inFlight && g_active.commandId != mcUnloadParsed.commandId)
		{
			rejectUnloadCommand(resultPath, mcUnloadParsed, "PRIOR_COMMAND_NON_TERMINAL",
				"A prior command is still executing.", inboxDigest);
			appendJournalEntry(journalPath, mcUnloadParsed.commandId);
			logRejectOnce(mcUnloadParsed.commandId, "PRIOR_COMMAND_NON_TERMINAL");
			return;
		}

		BattleUnit *actor = findUnitById(battle, mcUnloadParsed.actorUnitId);
		CompanionTacticalMcHostileEquipmentExecutionCore::McUnloadValidationInput mcInput = {};
		fillMcRemoteAuthorityInput(
			&mcInput.authority, game, battle, campaign, battleInitialized, controlJson, controlReadOk, sessionId,
			mcUnloadParsed, actor);
		mcInput.contract = CompanionTacticalMcHostileEquipmentExecutionCore::buildMcUnloadContractInput(
			game, battle, campaign, mcUnloadParsed, actor);
		const CompanionCommandCore::MoveValidationResult verdict =
			CompanionTacticalMcHostileEquipmentExecutionCore::validateMcUnloadExecutionPreflight(mcInput);
		if (!verdict.accepted)
		{
			rejectUnloadCommand(resultPath, mcUnloadParsed, verdict.rejectCode, verdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, mcUnloadParsed.commandId);
			logRejectOnce(mcUnloadParsed.commandId, verdict.rejectCode);
			return;
		}

		logCommandDiagnostic("accepted MC_UNLOAD command_id=" + mcUnloadParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(mcUnloadParsed.actorUnitId));

		BattleItem *weapon = findBattleItemByIdOnUnit(actor, mcUnloadParsed.weaponBattleItemId);
		const Position pos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		std::string rejectReason;
		if (!game->tryExecuteCompanionUnload(actor, weapon, mcUnloadParsed.loadedAmmoBattleItemId, &rejectReason))
		{
			rejectUnloadCommand(resultPath, mcUnloadParsed, "UNLOAD_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, mcUnloadParsed.commandId);
			logRejectOnce(mcUnloadParsed.commandId, "UNLOAD_START_FAILED");
			return;
		}
		const std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_COMPLETED);
		writeResultFile(resultPath, buildUnloadResultJson(mcUnloadParsed, CompanionCommandCore::CMD_COMPLETED, std::string(), pos, actor != 0 ? actor->getTimeUnits() : 0, actor != 0 && actor->isKneeled(), resultSnapshotSha256, inboxDigest, 8));
		logTerminalOnce(mcUnloadParsed.commandId, CompanionCommandCore::CMD_COMPLETED);
		appendJournalEntry(journalPath, mcUnloadParsed.commandId);
		return;
	}
	if (inboxCommandType == "MC_INVENTORY_MOVE")
	{
		if (!CompanionTacticalMcHostileEquipmentExecutionCore::mcEquipmentRelayExecutionSupported())
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			CompanionCommandCore::InventoryMoveCommandParsed unsupported = {};
			unsupported.rejectCode = "MC_EXECUTION_NOT_SUPPORTED";
			unsupported.commandType = inboxCommandType;
			CompanionJsonLite::extractJsonStringField(commandJson, "command_id", &unsupported.commandId);
			rejectInventoryMoveCommand(resultPath, unsupported, "MC_EXECUTION_NOT_SUPPORTED",
				"MC hostile equipment relay execution is not enabled.", inboxDigest);
			logRejectOnce(unsupported.commandId.empty() ? "unknown" : unsupported.commandId,
				"MC_EXECUTION_NOT_SUPPORTED");
			return;
		}

		CompanionCommandCore::InventoryMoveCommandParsed mcInvParsed = {};
		if (!CompanionCommandCore::parseMcInventoryMoveCommandJson(commandJson, &mcInvParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectInventoryMoveCommand(resultPath, mcInvParsed, mcInvParsed.rejectCode, mcInvParsed.rejectCode, inboxDigest);
			logRejectOnce(mcInvParsed.commandId.empty() ? "unknown" : mcInvParsed.commandId, mcInvParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(mcInvParsed.commandId);
		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, 0, 0, 0, 0, 0, 0, &mcInvParsed, 0, 0, 0))
		{
			logRejectOnce(mcInvParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, mcInvParsed.commandId);
		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, mcInvParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, mcInvParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, 0, 0, 0, 0, 0, 0, &mcInvParsed, 0, 0, 0))
		{
			return;
		}

		if (g_active.inFlight && g_active.commandId != mcInvParsed.commandId)
		{
			rejectInventoryMoveCommand(resultPath, mcInvParsed, "PRIOR_COMMAND_NON_TERMINAL",
				"A prior command is still executing.", inboxDigest);
			appendJournalEntry(journalPath, mcInvParsed.commandId);
			logRejectOnce(mcInvParsed.commandId, "PRIOR_COMMAND_NON_TERMINAL");
			return;
		}

		BattleUnit *actor = findUnitById(battle, mcInvParsed.actorUnitId);
		CompanionTacticalMcHostileEquipmentExecutionCore::McInventoryMoveValidationInput mcInput = {};
		fillMcRemoteAuthorityInput(
			&mcInput.authority, game, battle, campaign, battleInitialized, controlJson, controlReadOk, sessionId,
			mcInvParsed, actor);
		mcInput.contract = CompanionTacticalMcHostileEquipmentExecutionCore::buildMcInventoryMoveContractInput(
			game, battle, campaign, mcInvParsed, actor);
		const CompanionCommandCore::MoveValidationResult verdict =
			CompanionTacticalMcHostileEquipmentExecutionCore::validateMcInventoryMoveExecutionPreflight(mcInput);
		if (!verdict.accepted)
		{
			rejectInventoryMoveCommand(resultPath, mcInvParsed, verdict.rejectCode, verdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, mcInvParsed.commandId);
			logRejectOnce(mcInvParsed.commandId, verdict.rejectCode);
			return;
		}

		logCommandDiagnostic("accepted MC_INVENTORY_MOVE command_id=" + mcInvParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(mcInvParsed.actorUnitId));

		BattleItem *item = findBattleItemByIdOnUnit(actor, mcInvParsed.itemBattleItemId);
		RuleInventory *destRule = game->getMod()->getInventory(mcInvParsed.destinationSlot, false);
		const Position pos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		const int tuBefore = actor != 0 ? actor->getTimeUnits() : 0;
		std::string rejectReason;
		if (!game->tryExecuteCompanionInventoryMove(actor, item, destRule, mcInvParsed.destinationX, mcInvParsed.destinationY, &rejectReason))
		{
			rejectInventoryMoveCommand(resultPath, mcInvParsed, "INVENTORY_MOVE_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, mcInvParsed.commandId);
			logRejectOnce(mcInvParsed.commandId, "INVENTORY_MOVE_START_FAILED");
			return;
		}
		const int tuAfter = actor != 0 ? actor->getTimeUnits() : 0;
		const int tuSpent = tuBefore - tuAfter;
		if (tuSpent < 0)
		{
			rejectInventoryMoveCommand(resultPath, mcInvParsed, "INVENTORY_MOVE_START_FAILED",
				"Inconsistent TU accounting after inventory move.", inboxDigest);
			appendJournalEntry(journalPath, mcInvParsed.commandId);
			logRejectOnce(mcInvParsed.commandId, "INVENTORY_MOVE_START_FAILED");
			return;
		}
		const std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_COMPLETED);
		writeResultFile(resultPath, buildInventoryMoveResultJson(mcInvParsed, CompanionCommandCore::CMD_COMPLETED, std::string(), pos, actor != 0 ? actor->getTimeUnits() : 0, actor != 0 && actor->isKneeled(), resultSnapshotSha256, inboxDigest, tuSpent));
		logTerminalOnce(mcInvParsed.commandId, CompanionCommandCore::CMD_COMPLETED);
		appendJournalEntry(journalPath, mcInvParsed.commandId);
		return;
	}
	if (inboxCommandType == "MC_PICKUP")
	{
		if (!CompanionTacticalMcHostileEquipmentExecutionCore::mcEquipmentRelayExecutionSupported())
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			CompanionCommandCore::PickupCommandParsed unsupported = {};
			unsupported.rejectCode = "MC_EXECUTION_NOT_SUPPORTED";
			unsupported.commandType = inboxCommandType;
			CompanionJsonLite::extractJsonStringField(commandJson, "command_id", &unsupported.commandId);
			rejectPickupCommand(resultPath, unsupported, "MC_EXECUTION_NOT_SUPPORTED",
				"MC hostile equipment relay execution is not enabled.", inboxDigest);
			logRejectOnce(unsupported.commandId.empty() ? "unknown" : unsupported.commandId,
				"MC_EXECUTION_NOT_SUPPORTED");
			return;
		}

		CompanionCommandCore::PickupCommandParsed mcPickupParsed = {};
		if (!CompanionCommandCore::parseMcPickupCommandJson(commandJson, &mcPickupParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectPickupCommand(resultPath, mcPickupParsed, mcPickupParsed.rejectCode, mcPickupParsed.rejectCode, inboxDigest);
			logRejectOnce(mcPickupParsed.commandId.empty() ? "unknown" : mcPickupParsed.commandId, mcPickupParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(mcPickupParsed.commandId);
		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, 0, 0, 0, 0, 0, 0, 0, &mcPickupParsed, 0, 0))
		{
			logRejectOnce(mcPickupParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, mcPickupParsed.commandId);
		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, mcPickupParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, mcPickupParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, 0, 0, 0, 0, 0, 0, 0, &mcPickupParsed, 0, 0))
		{
			return;
		}

		if (g_active.inFlight && g_active.commandId != mcPickupParsed.commandId)
		{
			rejectPickupCommand(resultPath, mcPickupParsed, "PRIOR_COMMAND_NON_TERMINAL",
				"A prior command is still executing.", inboxDigest);
			appendJournalEntry(journalPath, mcPickupParsed.commandId);
			logRejectOnce(mcPickupParsed.commandId, "PRIOR_COMMAND_NON_TERMINAL");
			return;
		}

		BattleUnit *actor = findUnitById(battle, mcPickupParsed.actorUnitId);
		CompanionTacticalMcHostileEquipmentExecutionCore::McPickupValidationInput mcInput = {};
		fillMcRemoteAuthorityInput(
			&mcInput.authority, game, battle, campaign, battleInitialized, controlJson, controlReadOk, sessionId,
			mcPickupParsed, actor);
		mcInput.contract = CompanionTacticalMcHostileEquipmentExecutionCore::buildMcPickupContractInput(
			game, battle, campaign, mcPickupParsed, actor);
		const CompanionCommandCore::MoveValidationResult verdict =
			CompanionTacticalMcHostileEquipmentExecutionCore::validateMcPickupExecutionPreflight(mcInput);
		if (!verdict.accepted)
		{
			rejectPickupCommand(resultPath, mcPickupParsed, verdict.rejectCode, verdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, mcPickupParsed.commandId);
			logRejectOnce(mcPickupParsed.commandId, verdict.rejectCode);
			return;
		}

		logCommandDiagnostic("accepted MC_PICKUP command_id=" + mcPickupParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(mcPickupParsed.actorUnitId));

		BattleItem *groundItem = findGroundItemFromActorExport(actor, mcPickupParsed.itemBattleItemId);
		RuleInventory *destRule = game->getMod()->getInventory(mcPickupParsed.destinationSlot, false);
		const Position groundPos(mcPickupParsed.groundX, mcPickupParsed.groundY, mcPickupParsed.groundZ);
		const Position pos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		const int tuBefore = actor != 0 ? actor->getTimeUnits() : 0;
		std::string rejectReason;
		if (!game->tryExecuteCompanionPickup(actor, groundItem, groundPos, destRule, mcPickupParsed.destinationX, mcPickupParsed.destinationY, &rejectReason))
		{
			rejectPickupCommand(resultPath, mcPickupParsed, "PICKUP_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, mcPickupParsed.commandId);
			logRejectOnce(mcPickupParsed.commandId, "PICKUP_START_FAILED");
			return;
		}
		const int tuAfter = actor != 0 ? actor->getTimeUnits() : 0;
		const int tuSpent = tuBefore - tuAfter;
		if (tuSpent < 0)
		{
			rejectPickupCommand(resultPath, mcPickupParsed, "PICKUP_START_FAILED",
				"Inconsistent TU accounting after pickup.", inboxDigest);
			appendJournalEntry(journalPath, mcPickupParsed.commandId);
			logRejectOnce(mcPickupParsed.commandId, "PICKUP_START_FAILED");
			return;
		}
		const std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_COMPLETED);
		writeResultFile(resultPath, buildPickupResultJson(mcPickupParsed, CompanionCommandCore::CMD_COMPLETED, std::string(), pos, actor != 0 ? actor->getTimeUnits() : 0, actor != 0 && actor->isKneeled(), resultSnapshotSha256, inboxDigest, tuSpent));
		logTerminalOnce(mcPickupParsed.commandId, CompanionCommandCore::CMD_COMPLETED);
		appendJournalEntry(journalPath, mcPickupParsed.commandId);
		return;
	}
	if (inboxCommandType == "MC_DROP")
	{
		if (!CompanionTacticalMcHostileEquipmentExecutionCore::mcEquipmentRelayExecutionSupported())
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			CompanionCommandCore::DropCommandParsed unsupported = {};
			unsupported.rejectCode = "MC_EXECUTION_NOT_SUPPORTED";
			unsupported.commandType = inboxCommandType;
			CompanionJsonLite::extractJsonStringField(commandJson, "command_id", &unsupported.commandId);
			rejectDropCommand(resultPath, unsupported, "MC_EXECUTION_NOT_SUPPORTED",
				"MC hostile equipment relay execution is not enabled.", inboxDigest);
			logRejectOnce(unsupported.commandId.empty() ? "unknown" : unsupported.commandId,
				"MC_EXECUTION_NOT_SUPPORTED");
			return;
		}

		CompanionCommandCore::DropCommandParsed mcDropParsed = {};
		if (!CompanionCommandCore::parseMcDropCommandJson(commandJson, &mcDropParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectDropCommand(resultPath, mcDropParsed, mcDropParsed.rejectCode, mcDropParsed.rejectCode, inboxDigest);
			logRejectOnce(mcDropParsed.commandId.empty() ? "unknown" : mcDropParsed.commandId, mcDropParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(mcDropParsed.commandId);
		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, &mcDropParsed, 0))
		{
			logRejectOnce(mcDropParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, mcDropParsed.commandId);
		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, mcDropParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, mcDropParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, &mcDropParsed, 0))
		{
			return;
		}

		if (g_active.inFlight && g_active.commandId != mcDropParsed.commandId)
		{
			rejectDropCommand(resultPath, mcDropParsed, "PRIOR_COMMAND_NON_TERMINAL",
				"A prior command is still executing.", inboxDigest);
			appendJournalEntry(journalPath, mcDropParsed.commandId);
			logRejectOnce(mcDropParsed.commandId, "PRIOR_COMMAND_NON_TERMINAL");
			return;
		}

		BattleUnit *actor = findUnitById(battle, mcDropParsed.actorUnitId);
		CompanionTacticalMcHostileEquipmentExecutionCore::McDropValidationInput mcInput = {};
		fillMcRemoteAuthorityInput(
			&mcInput.authority, game, battle, campaign, battleInitialized, controlJson, controlReadOk, sessionId,
			mcDropParsed, actor);
		mcInput.contract = CompanionTacticalMcHostileEquipmentExecutionCore::buildMcDropContractInput(
			game, battle, campaign, mcDropParsed, actor);
		const CompanionCommandCore::MoveValidationResult verdict =
			CompanionTacticalMcHostileEquipmentExecutionCore::validateMcDropExecutionPreflight(mcInput);
		if (!verdict.accepted)
		{
			rejectDropCommand(resultPath, mcDropParsed, verdict.rejectCode, verdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, mcDropParsed.commandId);
			logRejectOnce(mcDropParsed.commandId, verdict.rejectCode);
			return;
		}

		logCommandDiagnostic("accepted MC_DROP command_id=" + mcDropParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(mcDropParsed.actorUnitId));

		BattleItem *item = findBattleItemByIdOnUnit(actor, mcDropParsed.itemBattleItemId);
		const Position pos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		const int tuBefore = actor != 0 ? actor->getTimeUnits() : 0;
		std::string rejectReason;
		if (!game->tryExecuteCompanionDrop(actor, item, &rejectReason))
		{
			rejectDropCommand(resultPath, mcDropParsed, "DROP_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, mcDropParsed.commandId);
			logRejectOnce(mcDropParsed.commandId, "DROP_START_FAILED");
			return;
		}
		const int tuAfter = actor != 0 ? actor->getTimeUnits() : 0;
		const int tuSpent = tuBefore - tuAfter;
		if (tuSpent < 0)
		{
			rejectDropCommand(resultPath, mcDropParsed, "DROP_START_FAILED",
				"Inconsistent TU accounting after drop.", inboxDigest);
			appendJournalEntry(journalPath, mcDropParsed.commandId);
			logRejectOnce(mcDropParsed.commandId, "DROP_START_FAILED");
			return;
		}
		CompanionDropGroundPlacementCore::GroundPlacementEvidence dropPlacement = {};
		CompanionDropGroundPlacementCore::captureAfterSuccessfulDrop(
			item, actor, mcDropParsed.itemBattleItemId, &dropPlacement);
		const std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_COMPLETED);
		writeResultFile(resultPath, buildDropResultJson(mcDropParsed, CompanionCommandCore::CMD_COMPLETED, std::string(), pos, actor != 0 ? actor->getTimeUnits() : 0, actor != 0 && actor->isKneeled(), resultSnapshotSha256, inboxDigest, tuSpent, dropPlacement.present ? &dropPlacement : 0));
		logTerminalOnce(mcDropParsed.commandId, CompanionCommandCore::CMD_COMPLETED);
		appendJournalEntry(journalPath, mcDropParsed.commandId);
		return;
	}