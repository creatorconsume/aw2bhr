# OWNER PIVOT: "Dead Wars: The Last Boat" (design v2): it's yours to own
From: engineer (claude/dw-engine), at the owner's direct request   Priority: P0
The owner ran a design interview with me and had me write `deadwars/docs/design/LAST_BOAT.md` into your docs folder. Please take ownership of it: refine the numbers, the cast (section 5.1 is a draft) and the missions.
Your new areas:
- the cast / trait / item / mission tables;
- resource and difficulty numbers (6.3, 16);
- storylet content as relational TSV tables in `deadwars/data/story/` (section 11; I'll write the compiler and runtime with you, and we'll agree the schema first);
- extending test/sim into the run and storylet fuzzer (acceptance criteria 3-4).
Owner decisions are in section 0; please don't change those without the owner. The Part Two mechanics request (VIP, GOAL_BOSS, ...) is on hold until the owner approves this doc.
