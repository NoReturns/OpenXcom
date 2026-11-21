
	if (inboxCommandType == "MC_PRIME")
	{
		if (!CompanionTacticalMcHostileEquipmentExecutionCore::mcEquipmentRelayExecutionSupported())
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			CompanionCommandCore::PrimeCommandParsed unsupported = {};
			unsupported.rejectCode = "MC_EXECUTION_NOT_SUPPORTED";
			unsupported.commandType = inboxCommandType;
			CompanionJsonLite::extractJsonStringField(commandJson, "command_id", &unsupported.commandId);
			rejectPrimeCommand(resultPath, unsupported, "MC_EXECUTION_NOT_SUPPORTED",
				"MC hostile equipment relay execution is not enabled.", inboxDigest);
			logRejectOnce(unsupported.commandId.empty() ? "unknown" : unsupported.commandId,
				"MC_EXECUTION_NOT_SUPPORTED");
			return;
		}

		CompanionCommandCore::PrimeCommandParsed mcPrimeParsed = {};
		if (!CompanionCommandCore::parseMcPrimeCommandJson(commandJson, &mcPrimeParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectPrimeCommand(resultPath, mcPrimeParsed, mcPrimeParsed.rejectCode, mcPrimeParsed.rejectCode, inboxDigest);
			logRejectOnce(mcPrimeParsed.commandId.empty() ? "unknown" : mcPrimeParsed.commandId, mcPrimeParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(mcPrimeParsed.commandId);
		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, 0, &mcPrimeParsed, 0, 0, 0, 0, 0, 0, 0, 0))
		{
			logRejectOnce(mcPrimeParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, mcPrimeParsed.commandId);
		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, mcPrimeParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, mcPrimeParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, 0, &mcPrimeParsed, 0, 0, 0, 0, 0, 0, 0, 0))
		{
			return;
		}

		if (g_active.inFlight && g_active.commandId != mcPrimeParsed.commandId)
		{
			rejectPrimeCommand(resultPath, mcPrimeParsed, "PRIOR_COMMAND_NON_TERMINAL",
				"A prior command is still executing.", inboxDigest);
			appendJournalEntry(journalPath, mcPrimeParsed.commandId);
			logRejectOnce(mcPrimeParsed.commandId, "PRIOR_COMMAND_NON_TERMINAL");
			return;
		}

		BattleUnit *actor = findUnitById(battle, mcPrimeParsed.actorUnitId);
		CompanionTacticalMcHostileEquipmentExecutionCore::McPrimeValidationInput primeInput = {};
		fillMcRemoteAuthorityInput(
			&primeInput.authority, game, battle, campaign, battleInitialized, controlJson, controlReadOk, sessionId,
			mcPrimeParsed, actor);
		primeInput.contract = CompanionTacticalMcHostileEquipmentExecutionCore::buildMcPrimeContractInput(
			game, battle, campaign, mcPrimeParsed, actor);
		const CompanionCommandCore::MoveValidationResult primeVerdict =
			CompanionTacticalMcHostileEquipmentExecutionCore::validateMcPrimeExecutionPreflight(primeInput);
		if (!primeVerdict.accepted)
		{
			rejectPrimeCommand(resultPath, mcPrimeParsed, primeVerdict.rejectCode, primeVerdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, mcPrimeParsed.commandId);
			logRejectOnce(mcPrimeParsed.commandId, primeVerdict.rejectCode);
			return;
		}

		logCommandDiagnostic("accepted MC_PRIME command_id=" + mcPrimeParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(mcPrimeParsed.actorUnitId));
		BattleItem *weapon = actor != 0 ? actor->getItem(mcPrimeParsed.activeHand) : 0;
		if (actor != 0 && actor->getActiveHand() != mcPrimeParsed.activeHand)
		{
			actor->setActiveHand(mcPrimeParsed.activeHand);
		}
		std::string rejectReason;
		if (!game->tryExecuteCompanionPrime(actor, weapon, mcPrimeParsed.fuseTimer, &rejectReason))
		{
			rejectPrimeCommand(resultPath, mcPrimeParsed, "PRIME_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, mcPrimeParsed.commandId);
			logRejectOnce(mcPrimeParsed.commandId, "PRIME_START_FAILED");
			return;
		}

		const std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_COMPLETED);
		writeResultFile(resultPath, buildPrimeResultJson(mcPrimeParsed, CompanionCommandCore::CMD_COMPLETED, std::string(),
			actor != 0 ? actor->getPosition() : Position(0, 0, 0),
			actor != 0 ? actor->getTimeUnits() : 0,
			actor != 0 && actor->isKneeled(),
			resultSnapshotSha256, inboxDigest));
		logTerminalOnce(mcPrimeParsed.commandId, CompanionCommandCore::CMD_COMPLETED);
		appendJournalEntry(journalPath, mcPrimeParsed.commandId);
		CompanionRelayPublicationFreshnessCore::noteTacticalBattleMutation();
		return;
	}

	if (inboxCommandType == "MC_THROW")
	{
		if (!CompanionTacticalMcHostileEquipmentExecutionCore::mcEquipmentRelayExecutionSupported())
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			CompanionCommandCore::ThrowCommandParsed unsupported = {};
			unsupported.rejectCode = "MC_EXECUTION_NOT_SUPPORTED";
			unsupported.commandType = inboxCommandType;
			CompanionJsonLite::extractJsonStringField(commandJson, "command_id", &unsupported.commandId);
			rejectThrowCommand(resultPath, unsupported, "MC_EXECUTION_NOT_SUPPORTED",
				"MC hostile equipment relay execution is not enabled.", inboxDigest);
			logRejectOnce(unsupported.commandId.empty() ? "unknown" : unsupported.commandId,
				"MC_EXECUTION_NOT_SUPPORTED");
			return;
		}

		CompanionCommandCore::ThrowCommandParsed mcThrowParsed = {};
		if (!CompanionCommandCore::parseMcThrowCommandJson(commandJson, &mcThrowParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectThrowCommand(resultPath, mcThrowParsed, mcThrowParsed.rejectCode, mcThrowParsed.rejectCode, inboxDigest);
			logRejectOnce(mcThrowParsed.commandId.empty() ? "unknown" : mcThrowParsed.commandId, mcThrowParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(mcThrowParsed.commandId);
		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, 0, 0, &mcThrowParsed, 0, 0, 0, 0, 0, 0, 0))
		{
			logRejectOnce(mcThrowParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, mcThrowParsed.commandId);
		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, mcThrowParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, mcThrowParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, 0, 0, &mcThrowParsed, 0, 0, 0, 0, 0, 0, 0))
		{
			return;
		}

		if (g_active.inFlight && g_active.commandId != mcThrowParsed.commandId)
		{
			rejectThrowCommand(resultPath, mcThrowParsed, "PRIOR_COMMAND_NON_TERMINAL",
				"A prior command is still executing.", inboxDigest);
			appendJournalEntry(journalPath, mcThrowParsed.commandId);
			logRejectOnce(mcThrowParsed.commandId, "PRIOR_COMMAND_NON_TERMINAL");
			return;
		}

		BattleUnit *actor = findUnitById(battle, mcThrowParsed.actorUnitId);
		CompanionTacticalMcHostileEquipmentExecutionCore::McThrowValidationInput throwInput = {};
		fillMcRemoteAuthorityInput(
			&throwInput.authority, game, battle, campaign, battleInitialized, controlJson, controlReadOk, sessionId,
			mcThrowParsed, actor);
		throwInput.contract = CompanionTacticalMcHostileEquipmentExecutionCore::buildMcThrowContractInput(
			game, battle, campaign, mcThrowParsed, actor);
		const CompanionCommandCore::MoveValidationResult throwVerdict =
			CompanionTacticalMcHostileEquipmentExecutionCore::validateMcThrowExecutionPreflight(throwInput);
		if (!throwVerdict.accepted)
		{
			rejectThrowCommand(resultPath, mcThrowParsed, throwVerdict.rejectCode, throwVerdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, mcThrowParsed.commandId);
			logRejectOnce(mcThrowParsed.commandId, throwVerdict.rejectCode);
			return;
		}

		logCommandDiagnostic("accepted MC_THROW command_id=" + mcThrowParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(mcThrowParsed.actorUnitId));

		beginInFlightCommand();
		g_active.kind = ActiveCommand::KIND_MC_THROW;
		g_active.commandId = mcThrowParsed.commandId;
		g_active.inboxDigest = inboxDigest;
		g_active.throwParsed = mcThrowParsed;
		g_active.mcEquipmentAuthority = throwInput.authority;
		g_active.startPos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		g_active.snapProjectileLaunched = false;
		g_active.snapActionResult.clear();

		BattleItem *weapon = actor != 0 ? actor->getItem(mcThrowParsed.activeHand) : 0;
		const Position target(mcThrowParsed.targetX, mcThrowParsed.targetY, mcThrowParsed.targetZ);
		if (actor != 0 && actor->getActiveHand() != mcThrowParsed.activeHand)
		{
			actor->setActiveHand(mcThrowParsed.activeHand);
		}
		std::string rejectReason;
		if (!game->tryExecuteCompanionThrow(actor, target, weapon, &rejectReason))
		{
			g_active.inFlight = false;
			g_active.kind = ActiveCommand::KIND_NONE;
			rejectThrowCommand(resultPath, mcThrowParsed, "THROW_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, mcThrowParsed.commandId);
			logRejectOnce(mcThrowParsed.commandId, "THROW_START_FAILED");
			return;
		}

		beginInFlightCommand();
		g_active.kind = ActiveCommand::KIND_MC_THROW;
		logSnapShotStartedOnce(mcThrowParsed.commandId, mcThrowParsed.actorUnitId);
		writeResultFile(resultPath, buildThrowResultJson(mcThrowParsed, CompanionCommandCore::CMD_EXECUTING, std::string(),
			g_active.startPos, g_active.startPos, actor->getTimeUnits(), actor->isKneeled(), g_lastSnapshotSha256, inboxDigest));
		appendJournalEntry(journalPath, mcThrowParsed.commandId);
		return;
	}