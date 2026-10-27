/* camera_third_person.h: the camera held in third person with a melee weapon
(port/linux/game/camera_third_person.c)

With game.third_person_melee ("auto" by default) a unit holding a melee
weapon (one with no magazines) is followed by the third-person camera, where
Halo would show the first person. "auto" is on for a Shadowrun prototype map
and off elsewhere; "true" and "false" hold either everywhere. Never the
netcode's deterministic camera. */

#ifndef HALO_CAMERA_THIRD_PERSON_H
#define HALO_CAMERA_THIRD_PERSON_H

/* whether the camera should follow this unit in third person (game.third_person_melee)
*/
boolean camera_third_person_wanted(long unit_index);

#endif
