/* GPL-2.0-or-later. Intentionally included twice: live and legacy save layouts.
 * Keep the legacy branch's field order/types unchanged for save compatibility. */
#ifdef DOOM_MOBJ_COMPACT_FIELDS

    // Preserve the sound-origin (degenmobj_t) common prefix exactly.
    thinker_t thinker;
    fixed_t x,y,z;
    angle_t angle;
    uint_64_t flags;
    struct mobj_s *snext, **sprev;
    int frame;
    struct mobj_s *bnext, **bprev;
    struct subsector_s *subsector;
    fixed_t floorz,ceilingz,dropoffz,radius,height,momx,momy,momz;
    int validcount,tics,intflags,health;
    struct mobj_s *target;
    struct player_s *player;
    struct mobj_s *tracer,*lastenemy;
    int friction,movefactor;
    struct msecnode_s *touching_sectorlist;
    fixed_t PrevX,PrevY,PrevZ;
    short movedir,movecount,strafecount,reactiontime,threshold,pursuecount,gear,lastlook;
    mapthing_t spawnpoint;
    unsigned short type,state_index;
    short sprite;
#else

    // List: thinker links.
    thinker_t           thinker;

    // Info for drawing: position.
    fixed_t             x;
    fixed_t             y;
    fixed_t             z;

    // More list: links in sector (if needed)
    struct mobj_s*      snext;
    struct mobj_s**     sprev; // killough 8/10/98: change to ptr-to-ptr

    //More drawing info: to determine current sprite.
    angle_t             angle;  // orientation
    spritenum_t         sprite; // used to find patch_t and flip value
    int                 frame;  // might be ORed with FF_FULLBRIGHT

    // Interaction info, by BLOCKMAP.
    // Links in blocks (if needed).
    struct mobj_s*      bnext;
    struct mobj_s**     bprev; // killough 8/11/98: change to ptr-to-ptr

    struct subsector_s* subsector;

    // The closest interval over all contacted Sectors.
    fixed_t             floorz;
    fixed_t             ceilingz;

    // killough 11/98: the lowest floor over all contacted Sectors.
    fixed_t             dropoffz;

    // For movement checking.
    fixed_t             radius;
    fixed_t             height;

    // Momentums, used to update position.
    fixed_t             momx;
    fixed_t             momy;
    fixed_t             momz;

    // If == validcount, already checked.
    int                 validcount;

    mobjtype_t          type;
    mobjinfo_t*         info;   // &mobjinfo[mobj->type]

    int                 tics;   // state tic counter
    state_t*            state;
    uint_64_t           flags;
    int                 intflags;  // killough 9/15/98: internal flags
    int                 health;

    // Movement direction, movement generation (zig-zagging).
    short               movedir;        // 0-7
    short               movecount;      // when 0, select a new dir
    short               strafecount;    // killough 9/8/98: monster strafing

    // Thing being chased/attacked (or NULL),
    // also the originator for missiles.
    struct mobj_s*      target;

    // Reaction time: if non 0, don't attack yet.
    // Used by player to freeze a bit after teleporting.
    short               reactiontime;

    // If >0, the current target will be chased no
    // matter what (even if shot by another object)
    short               threshold;

    // killough 9/9/98: How long a monster pursues a target.
    short               pursuecount;

    short               gear; // killough 11/98: used in torque simulation

    // Additional info record for player avatars only.
    // Only valid if type == MT_PLAYER
    struct player_s*    player;

    // Player number last looked for.
    short               lastlook;

    // For nightmare respawn.
    mapthing_t          spawnpoint;

    // Thing being chased/attacked for tracers.
    struct mobj_s*      tracer;

    // new field: last known enemy -- killough 2/15/98
    struct mobj_s*      lastenemy;

    // killough 8/2/98: friction properties part of sectors,
    // not objects -- removed friction properties from here
    // e6y: restored friction properties here
    // Friction values for the sector the object is in
    int friction;                                           // phares 3/17/98
    int movefactor;

    // a linked list of sectors where this object appears
    struct msecnode_s* touching_sectorlist;                 // phares 3/14/98

    fixed_t             PrevX;
    fixed_t             PrevY;
    fixed_t             PrevZ;

    fixed_t             pad; // cph - needed so I can get the size unambiguously on amd64

    // SEE WARNING ABOVE ABOUT POINTER FIELDS!!!
#endif
