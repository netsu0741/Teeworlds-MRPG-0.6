# Health and mana magnet module

The magnet module (item 185) attracts map health and armor pickups from tiles
193 and 192, respectively. It no longer attracts dropped inventory items.
Only the map-spawned pickup entities participate; mob loot, weapons, and
pickups created by other systems are unchanged. A pickup travels toward the
character's orbit before joining it. The original spawn becomes available
again on its normal respawn timer. If the owner disconnects, dies, changes
world, or unequips the magnet before arrival, the pickup returns to its spawn.

The inventory item menu offers two free upgrades: a new module starts at level
1, and levels 2 and 3 add one health and one mana orbit slot each. No upgrade
cost was specified, so these upgrades intentionally cost no currency. Each
visible heart or shield represents up to five individual stored points; the
maximum at level 3 is 20 health and 20 mana. Incoming pickups count against
capacity so simultaneous attraction cannot overflow it. A full resource type
is left on the map and can still be picked up normally when appropriate.

The reserve is temporary, character-local state: it is not saved across death,
world change, or disconnect. It remains held but inactive while the module is
unequipped. Each pickup stored in orbit restores one point of its resource,
not the normal map pickup's percentage-based restoration. Damage and mana
spending consume the reserve immediately; empty orbit slots disappear and can
be filled again. Hearts and shields alternate when both are present.

The SQL dump updates the default description for new installations. Existing
databases retain their item descriptions unless an operator updates item 185;
no automatic database migration is performed.
