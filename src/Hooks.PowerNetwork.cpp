// Power network -- radius powering with relay chains ("power lines") and per-source
// capacity, for REGULAR TechnoTypes (units and buildings), not just attachments.
//
// A source powers consumers within PowerSource.Range cells. A relay that is itself
// reached re-broadcasts within its own range, so a chain of relays carries power
// across the map. Each source sustains at most PowerSource.Count consumers; past
// that, OverflowMode decides whether the whole group drops (all) or only the ones
// beyond the cap (excess).
//
// Consumers go dark through the SAME arbiter as attachment power
// (TechnoExt::DeactivationReasons), so radius power, attachment power, EMP and
// vanilla PoweredUnit never fight over the Deactivated flag.
//
// DETERMINISM (online-safe): solved once per frame, in TechnoClass::Array index
// order throughout -- source selection, relay claiming and capacity cut-off all
// break ties by array index, never by pointer value or map iteration order. No RNG,
// no wall-clock. The result is derived state recomputed every frame, so it needs no
// serialization.
//
// Hook: 0x55B6B3 (LogicClass::AI, immediately after the object-update loop).
// Encyclopedia-checked: the only other consumer is Phobos PR #352 (unmerged), so
// no release framework (Phobos/Antares/Kratos) collides here.

#include <TechnoClass.h>
#include <HouseClass.h>
#include <BuildingClass.h>
#include <BuildingTypeClass.h>
#include <HouseTypeClass.h>

#include <Utilities/Macro.h>

#include <Ext/Techno/Body.h>
#include <Ext/TechnoType/Body.h>

#include <vector>

namespace TAExtPowerNetwork
{
	// Set once the solver has run at least once. Consumers refuse to go dark before
	// that, so a solver that never runs fails SAFE (powered) instead of darkening
	// every consumer on the map.
	bool Solved = false;

	// Defined below; declared here because the building-power query above uses it.
	static int DistSq(const CellStruct& a, const CellStruct& b);

	// --- external-structure power (vanilla PowersUnit semantics) ---------------
	// Snapshot of every live building, rebuilt each frame alongside the network so
	// the per-attachment PoweredBy gate is a cheap lookup instead of an O(buildings)
	// scan per attachment. "Powered" mirrors BuildingClass::HasPower AND the
	// building not being dark itself, so a shut-down plant stops powering things.
	struct BuildingRecord
	{
		HouseClass* Owner;
		BuildingTypeClass* Type;
		CellStruct Coords;
		bool Powered;
	};
	static std::vector<BuildingRecord> buildings;

	// Is pOther's relationship to pSelf one the modder asked for? See
	// TAExtHouseRelation. Passive (MultiplayPassive) countries are excluded from
	// "enemy" so neutrals/civilians never read as hostile -- ask for them by name.
	static bool HouseRelationMatches(HouseClass* pSelf, HouseClass* pOther, int mask)
	{
		if (!pSelf || !pOther)
			return false;

		if (mask & TAExtHouse_Any)
			return true;

		bool const isSelf = (pSelf == pOther);

		if ((mask & TAExtHouse_Owner) && isSelf)
			return true;

		if ((mask & TAExtHouse_Ally) && !isSelf && pSelf->IsAlliedWith(pOther))
			return true;

		if ((mask & TAExtHouse_Team) && !isSelf)
		{
			// Only meaningful in team/tournament games; 0 means "no team", so two
			// teamless houses must NOT read as team-mates.
			int const team = pSelf->TournamentTeamID;
			if (team != 0 && pOther->TournamentTeamID == team)
				return true;
		}

		auto const pOtherType = pOther->Type;
		bool const passive = pOtherType && pOtherType->MultiplayPassive;

		if ((mask & TAExtHouse_Neutral) && pOtherType
			&& _strcmpi(pOtherType->ID, "Neutral") == 0)
			return true;

		if ((mask & TAExtHouse_Special) && pOtherType
			&& _strcmpi(pOtherType->ID, "Special") == 0)
			return true;

		if ((mask & TAExtHouse_Civilian) && passive)
			return true;

		if ((mask & TAExtHouse_Enemy) && !isSelf && !passive && !pSelf->IsAlliedWith(pOther))
			return true;

		return false;
	}

	// Does a qualifying building of pType exist for pOwner? houseMask decides whose
	// buildings count (default owner-only = vanilla PowersUnit). rangeCells <= 0
	// means house-wide; otherwise it must be within that many cells of `from`.
	bool HasWorkingBuilding(HouseClass* pOwner, BuildingTypeClass* pType,
		bool requirePower, int rangeCells, const CellStruct& from, int houseMask)
	{
		if (!pOwner || !pType)
			return false;

		for (auto const& rec : buildings)
		{
			if (rec.Type != pType)
				continue;
			if (!HouseRelationMatches(pOwner, rec.Owner, houseMask))
				continue;
			if (requirePower && !rec.Powered)
				continue;
			if (rangeCells > 0 && DistSq(rec.Coords, from) > rangeCells * rangeCells)
				continue;
			return true;
		}

		return false;
	}

	// Squared cell distance; integer math only (no floating point) so every peer
	// computes bit-identical results.
	static int DistSq(const CellStruct& a, const CellStruct& b)
	{
		int const dx = a.X - b.X;
		int const dy = a.Y - b.Y;
		return dx * dx + dy * dy;
	}

	struct Node
	{
		TechnoClass* Techno;
		CellStruct Coords;
		int Range;       // broadcast range in cells (source or relay)
		int RootSource;  // index into Sources of the source feeding this node
	};

	// Rebuilt every frame. Kept at namespace scope only to avoid re-allocating.
	static std::vector<Node> broadcasters;   // sources first, then claimed relays
	static std::vector<size_t> sourceOfNode; // broadcaster -> index into sources
	static std::vector<TechnoClass*> sources;
	static std::vector<int> sourceLoad;      // consumers assigned per source

	static bool IsUsable(TechnoClass* pTechno)
	{
		return pTechno && pTechno->IsAlive && !pTechno->InLimbo;
	}

	// Does a source/relay owned by pSource power consumer pConsumer? Both type
	// filters must accept: the source's PowerSource.Types (empty = any consumer)
	// and the consumer's PowerConsumer.Types (empty = any source).
	static bool TypesMatch(TechnoClass* pSource, TechnoClass* pConsumer)
	{
		auto const pSrcExt = TechnoTypeExt::ExtMap.Find(pSource->GetTechnoType());
		auto const pConExt = TechnoTypeExt::ExtMap.Find(pConsumer->GetTechnoType());
		if (!pSrcExt || !pConExt)
			return false;

		auto const& srcTypes = pSrcExt->PowerSource_Types;
		if (!srcTypes.empty())
		{
			auto const pConType = pConsumer->GetTechnoType();
			if (std::find(srcTypes.begin(), srcTypes.end(), pConType) == srcTypes.end())
				return false;
		}

		auto const& conTypes = pConExt->PowerConsumer_Types;
		if (!conTypes.empty())
		{
			auto const pSrcType = pSource->GetTechnoType();
			if (std::find(conTypes.begin(), conTypes.end(), pSrcType) == conTypes.end())
				return false;
		}

		return true;
	}

	void Solve()
	{
		broadcasters.clear();
		sourceOfNode.clear();
		sources.clear();
		sourceLoad.clear();

		auto const& array = TechnoClass::Array;

		// --- 0. Snapshot live buildings for the PoweredBy gate. -----------------
		buildings.clear();
		for (int i = 0; i < BuildingClass::Array.Count; ++i)
		{
			auto const pBld = BuildingClass::Array.GetItem(i);
			if (!pBld || !pBld->IsAlive || pBld->InLimbo)
				continue;

			buildings.push_back(BuildingRecord {
				pBld->Owner,
				pBld->Type,
				pBld->GetMapCoords(),
				pBld->HasPower && !pBld->Deactivated });
		}

		// --- 1. Seed broadcasters from sources (array order = deterministic). ---
		for (int i = 0; i < array.Count; ++i)
		{
			auto const pTechno = array.GetItem(i);
			if (!IsUsable(pTechno))
				continue;

			auto const pTypeExt = TechnoTypeExt::ExtMap.Find(pTechno->GetTechnoType());
			if (!pTypeExt || !pTypeExt->PowerSource)
				continue;

			size_t const srcIdx = sources.size();
			sources.push_back(pTechno);
			sourceLoad.push_back(0);
			broadcasters.push_back(Node {
				pTechno, pTechno->GetMapCoords(), pTypeExt->PowerSource_Range, static_cast<int>(srcIdx) });
			sourceOfNode.push_back(srcIdx);
		}

		// --- 2. Relay expansion: repeatedly claim unclaimed relays that fall inside
		// some broadcaster's radius. First claimer (in broadcaster order) wins, so
		// the chain's ownership is deterministic. Runs to a fixpoint; the frontier
		// only grows, and each relay is claimed at most once, so this terminates.
		std::vector<TechnoClass*> relays;
		std::vector<char> claimed;
		for (int i = 0; i < array.Count; ++i)
		{
			auto const pTechno = array.GetItem(i);
			if (!IsUsable(pTechno))
				continue;

			auto const pTypeExt = TechnoTypeExt::ExtMap.Find(pTechno->GetTechnoType());
			if (!pTypeExt || !pTypeExt->PowerRelay || pTypeExt->PowerSource)
				continue; // a source is already a broadcaster; don't double-count

			relays.push_back(pTechno);
			claimed.push_back(0);
		}

		for (size_t scan = 0; scan < broadcasters.size(); ++scan)
		{
			// Note: broadcasters grows inside this loop as relays join, which is
			// exactly the chain propagation ("power lines").
			auto const node = broadcasters[scan]; // copy: vector may reallocate
			if (node.Range <= 0)
				continue;
			int const rangeSq = node.Range * node.Range;

			for (size_t r = 0; r < relays.size(); ++r)
			{
				if (claimed[r])
					continue;

				auto const pRelay = relays[r];
				if (DistSq(node.Coords, pRelay->GetMapCoords()) > rangeSq)
					continue;

				auto const pRelayExt = TechnoTypeExt::ExtMap.Find(pRelay->GetTechnoType());
				if (!pRelayExt)
					continue;

				claimed[r] = 1;
				int const relayRange = pRelayExt->PowerRelay_Range >= 0
					? pRelayExt->PowerRelay_Range
					: node.Range; // unset -> inherit the feeding node's range

				broadcasters.push_back(Node {
					pRelay, pRelay->GetMapCoords(), relayRange, node.RootSource });
				sourceOfNode.push_back(static_cast<size_t>(node.RootSource));
			}
		}

		// --- 3. Assign consumers to the first covering broadcaster (array order),
		// honouring per-source capacity. ---
		struct Assignment { TechnoClass* Consumer; int Source; };
		std::vector<Assignment> assignments;

		for (int i = 0; i < array.Count; ++i)
		{
			auto const pTechno = array.GetItem(i);
			if (!IsUsable(pTechno))
				continue;

			auto const pTypeExt = TechnoTypeExt::ExtMap.Find(pTechno->GetTechnoType());
			if (!pTypeExt || !pTypeExt->PowerConsumer)
				continue;

			auto const coords = pTechno->GetMapCoords();
			int assigned = -1;

			for (size_t b = 0; b < broadcasters.size(); ++b)
			{
				auto const& node = broadcasters[b];
				if (node.Range <= 0)
					continue;
				if (DistSq(node.Coords, coords) > node.Range * node.Range)
					continue;

				auto const pRootSource = sources[sourceOfNode[b]];
				// House filter, both directions -- the consumer says whose network may
				// power it (PowerConsumer.House) and the source says whom it is willing
				// to power (PowerSource.House). Both must accept, mirroring how the
				// .Types filters compose. Default owner+ally = the original behaviour.
				if (!pRootSource->Owner || !pTechno->Owner)
					continue;

				auto const pSrcTypeExt = TechnoTypeExt::ExtMap.Find(pRootSource->GetTechnoType());
				if (!pSrcTypeExt)
					continue;

				if (!HouseRelationMatches(pTechno->Owner, pRootSource->Owner, pTypeExt->PowerConsumer_House))
					continue;
				if (!HouseRelationMatches(pRootSource->Owner, pTechno->Owner, pSrcTypeExt->PowerSource_House))
					continue;
				if (!TypesMatch(pRootSource, pTechno))
					continue;

				assigned = static_cast<int>(sourceOfNode[b]);
				break;
			}

			if (assigned >= 0)
			{
				++sourceLoad[assigned];
				assignments.push_back(Assignment { pTechno, assigned });
			}
			else
			{
				assignments.push_back(Assignment { pTechno, -1 });
			}
		}

		// --- 4. Apply capacity, then publish the result to each consumer. ---
		std::vector<int> served(sources.size(), 0);

		for (auto const& a : assignments)
		{
			bool powered = false;

			if (a.Source >= 0)
			{
				auto const pSrcExt = TechnoTypeExt::ExtMap.Find(sources[a.Source]->GetTechnoType());
				int const cap = pSrcExt ? pSrcExt->PowerSource_Count : 0;

				if (cap <= 0)
				{
					powered = true; // unlimited
				}
				else if (pSrcExt->PowerSource_OverflowAll)
				{
					// "all": exceeding the cap drops the source's whole group.
					powered = (sourceLoad[a.Source] <= cap);
				}
				else
				{
					// "excess": the first `cap` consumers (array order) stay powered.
					powered = (served[a.Source] < cap);
				}

				++served[a.Source];
			}

			if (auto const pExt = TechnoExt::ExtMap.Find(a.Consumer))
				pExt->NetworkPowered = powered;
		}

		Solved = true;
	}
}

DEFINE_HOOK(0x55B6B3, LogicClass_AI_After_SolvePowerNetwork_TAExt, 0x5)
{
	TAExtPowerNetwork::Solve();
	return 0;
}
