/*
 Copyright (c) 2013 yvt
 Modified by Paratrooper

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.
 */

 namespace spades {
    class ViewRifleSpring {
        double position = 0; 
        double desired = 0;
        double velocity = 0;
        double frequency = 1;
        double damping = 1;
        
        ViewRifleSpring() {}

        ViewRifleSpring(double f, double d) {
            frequency = f;
            damping = d;
        }

        ViewRifleSpring(double f, double d, double des) {
            frequency = f;
            damping = d;
            desired = des;
        }

        void Update(double updateLength) {
            // Forces updates into at least 240 fps.
            for (double timeLeft = updateLength; timeLeft > 0; timeLeft -= 1.0/240.0) {
                double dt = Min(1.0/240.0, timeLeft); 
                double acceleration = (desired - position) * frequency;
                velocity = velocity + acceleration * dt;
                velocity -= velocity * damping * dt;
                position = position + velocity * dt;
            }
        }
    }

    class ViewRifleEvent {
        bool activated = false;
        bool acknowledged = false;

        void Activate() {
            if (!acknowledged) {
                activated = true;
            }
        }

        bool WasActivated() {
            if (!acknowledged) {
                return activated;
            } else {
                return false;
            }
        }

        void Acknowledge() {
            acknowledged = true;
        }

        void Reset() {
            activated = false;
            acknowledged = false;
        }
    }

    class ViewRifleSkin:
    IToolSkin, IViewToolSkin, IWeaponSkin,
    BasicViewWeapon {

        private AudioDevice@ audioDevice;
        private Model@ gunModel;
        private Model@ magazineModel;
        private Model@ chargingHandleModel;
        private Model@ rearSightModel;
        private Model@ frontSightModel;

        private Image@[] muzzleFlashes(20);

        private AudioChunk@ fireFarSound;
        private AudioChunk@ fireStereoSound;
        private AudioChunk@ fireSmallReverbSound;
		private AudioChunk@ fireLargeReverbSound;
        private AudioChunk@ reloadSound;

        // Constants
        // Pivots
        // Weapon
        private Vector3 pivot = Vector3(1.5, 30.0, 6.0);
        // Magazine
        private Vector3 magazinePivot = Vector3(1.5, 5.5, -0.5);
        // Charging Handle
        private Vector3 chargingHandlePivot = Vector3(9.5, -0.5, 3.5);

        // Attachment Points
        // Magazine
        private Vector3 magazineAttachment = Vector3(1.5, 42.5, 6.0);
        // Rear Sights
        private Vector3 rearSightAttachment = Vector3(1.5, 28.6, 0.4);
        // Front Sights
        private Vector3 frontSightAttachment = Vector3(1.5, 94.0, -0.5);
        // Charging Handle
        private Vector3 chargingHandleAttachment = Vector3(1.5, 38.5, 1.5);
        
        // Scale
        // Weapon and global scale multiplier.
        private float globalScale = 0.015;
        // Magazine
        private float magazineScale = 0.5;
        // Rear Sights
        private float rearSightScale = 0.25;
        // Front Sights
        private float frontSightScale = 0.125;
        // Charging Handle
        // Slightly less than 0.5 to avoid z-fighting with the body.
        private float chargingHandleScale = 0.499;

        // A bunch of springs.
        private ViewRifleSpring recoilVerticalSpring = ViewRifleSpring(200, 24);
        private ViewRifleSpring recoilBackSpring = ViewRifleSpring(100, 16);
        private ViewRifleSpring recoilRotationSpring = ViewRifleSpring(50, 8);
        private ViewRifleSpring horizontalSwingSpring = ViewRifleSpring(100, 12);
        private ViewRifleSpring verticalSwingSpring = ViewRifleSpring(100, 12);
        private ViewRifleSpring reloadPitchSpring = ViewRifleSpring(150, 12, 0);
        private ViewRifleSpring reloadRollSpring = ViewRifleSpring(150, 16, 0);
        private ViewRifleSpring reloadOffsetSpring = ViewRifleSpring(150, 12, 0);
        private ViewRifleSpring sprintSpring = ViewRifleSpring(100, 10, 0);
        private ViewRifleSpring raiseSpring = ViewRifleSpring(200, 20, 1);
        private Vector3 swingFromSpring = Vector3();

        // A bunch of events.
        private ViewRifleEvent magazineTouched = ViewRifleEvent();
        private ViewRifleEvent magazineRemoved = ViewRifleEvent();
        private ViewRifleEvent magazineInserted = ViewRifleEvent();
        private ViewRifleEvent chargingHandlePulled = ViewRifleEvent();

        // A bunch of states.
        private double lastSprintState = 0;
        private double lastRaiseState = 0;
        
        // I need trueReloadProgress because reloadProgress
        // is choppy.
        private double trueReloadProgress = 2.5;

        // Creates a rotation matrix from euler angles (in the form of a Vector3) x-y-z
        Matrix4 CreateEulerAnglesMatrix( Vector3 angles ) {
            Matrix4 mat = CreateRotateMatrix( Vector3(1.0, 0.0, 0.0), angles.x );
            mat = CreateRotateMatrix( Vector3(0.0, 1.0, 0.0), angles.y ) * mat;
            mat = CreateRotateMatrix( Vector3(0.0, 0.0, 1.0), angles.z ) * mat;
            
            return mat;
        }

        Matrix4 AdjustToReload(Matrix4 mat) {
            if (trueReloadProgress < 0.60) {
                reloadPitchSpring.desired = 0.6;
                reloadRollSpring.desired = 0.6;
            } else if (trueReloadProgress < 0.90) {
                reloadPitchSpring.desired = 0.0;
                reloadRollSpring.desired = -0.6;
            } else {
                reloadPitchSpring.desired = 0;
                reloadRollSpring.desired = 0;
            }

            if (magazineTouched.WasActivated()) {
                magazineTouched.Acknowledge();
                reloadPitchSpring.velocity = 4;
            }

            if (magazineRemoved.WasActivated()) {
                magazineRemoved.Acknowledge();
                reloadPitchSpring.velocity = -4;
            }

            if (magazineInserted.WasActivated()) {
                magazineInserted.Acknowledge();
                reloadPitchSpring.velocity = 8;
            }

            if (chargingHandlePulled.WasActivated()) {
                chargingHandlePulled.Acknowledge();
                reloadPitchSpring.velocity = 5;
                reloadOffsetSpring.velocity = 2;
            }
            
            mat *= CreateEulerAnglesMatrix( Vector3(0.0, 0.6, 00) * reloadRollSpring.position );
            mat *= CreateEulerAnglesMatrix( Vector3(-1, 0.0, 0.0) * reloadPitchSpring.position );
            mat *= CreateTranslateMatrix( Vector3(0.0, -1, 0) * reloadOffsetSpring.position );

            return mat;
        }

        Vector3 GetMagazineOffset() {
            if (trueReloadProgress < 0.20) {
                return magazineAttachment - pivot;
            } else if (trueReloadProgress < 0.25) {
                magazineRemoved.Activate();
                return Mix(
                    magazineAttachment - pivot,
                    magazineAttachment - pivot + Vector3(0.0, -20.0, 50.0),
                    SmoothStep(Min(1.0, (trueReloadProgress-0.20) / 0.05))
                );
            } else if (trueReloadProgress < 0.40) {
                return magazineAttachment - pivot + Vector3(0.0, -20.0, 50.0);
            } else if (trueReloadProgress < 0.50) {
                return Mix(
                    magazineAttachment - pivot + Vector3(0.0, -20.0, 50.0),
                    magazineAttachment - pivot,
                    SmoothStep(Min(1.0, (trueReloadProgress-0.4) / 0.1))
                );
            } else {
                magazineInserted.Activate();
                return magazineAttachment - pivot;
            }
        }

        Vector3 GetLeftHandOffset() {
            if (trueReloadProgress < 0.10) {
                return Mix(
                    Vector3(4.0, 55.0, 9.0)-pivot,
                    GetMagazineOffset() + Vector3(0, 0, 15.0),
                    SmoothStep(Min(1.0, (trueReloadProgress) / 0.10))
                );
            } else if (trueReloadProgress < 0.60) {
                magazineTouched.Activate();
                return GetMagazineOffset() + Vector3(0, 0, 15.0);
            } else if (trueReloadProgress < 1.0) {
                return Mix(
                    GetMagazineOffset() + Vector3(0, 0, 15.0),
                    Vector3(4.0, 55.0, 9.0)-pivot,
                    SmoothStep(Min(1.0, (trueReloadProgress-0.60) / 0.10))
                );
            } else {
                return Vector3(4.0, 55.0, 9.0)-pivot;
            }
        }

        Vector3 GetRightHandOffset() {
            Vector3 chargingHandleOffset = chargingHandleAttachment-pivot + Vector3(-4, 16, 0);

            if (trueReloadProgress < 0.70) {
                return Vector3(-0.5, 30.0, 12.0)-pivot;
            } else if (trueReloadProgress < 0.80) {
                return Mix(
                    Vector3(-0.5, 30.0, 12.0)-pivot,
                    chargingHandleOffset,
                    SmoothStep(Min(1.0, (trueReloadProgress-0.70) / 0.10))
                );
            } else if (trueReloadProgress < 0.82) {
                return chargingHandleOffset;
            } else if (trueReloadProgress < 0.87) {
                chargingHandlePulled.Activate();
                return Mix(
                    chargingHandleOffset,
                    chargingHandleOffset + Vector3(0, -6, 0),
                    SmoothStep(Min(1.0, (trueReloadProgress-0.82) / 0.05))
                );
            } else if (trueReloadProgress < 1) {
                return Mix(
                    chargingHandleOffset + Vector3(0, -6, 0),
                    Vector3(-0.5, 30.0, 12.0)-pivot,
                    SmoothStep(Min(1.0, (trueReloadProgress-0.87) / 0.13))
                );
            } else {
                return Vector3(-0.5, 30.0, 12.0)-pivot;
            }
        }

        ViewRifleSkin(Renderer@ r, AudioDevice@ dev){
            super(r);
            @audioDevice = dev;
            @gunModel = renderer.RegisterModel
                ("Models/Weapons/Rifle/WeaponNoMagazine.kv6");
            @magazineModel = renderer.RegisterModel
                ("Models/Weapons/Rifle/Magazine.kv6");
            @rearSightModel = renderer.RegisterModel
                ("Models/Weapons/Rifle/RearSight.kv6");
            @frontSightModel = renderer.RegisterModel
                ("Models/Weapons/Rifle/FrontSight.kv6");
            @chargingHandleModel = renderer.RegisterModel
                ("Models/Weapons/Rifle/ChargingHandle.kv6");

            @fireFarSound = dev.RegisterSound
                ("Sounds/Weapons/Rifle/FireFar.wav");
            @fireStereoSound = dev.RegisterSound
                ("Sounds/Weapons/Rifle/FireStereo.wav");
            @reloadSound = dev.RegisterSound
                ("Sounds/Weapons/Rifle/ReloadLocal.wav");
			
			@fireSmallReverbSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/V2AmbienceSmall.wav");
			@fireLargeReverbSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/V2AmbienceLarge.wav");
                
            for ( uint i = 0; i < 20; i++ ) {
                string path = "Gfx/Flash/Weapons/Rifle/";
                path += i / 100;            // hundreds
                path += i % 100 / 10;       // tens
                path += i % 10;             // units
                path += ".png";
                @muzzleFlashes[i] = renderer.RegisterImage(path);
            }
            
            raiseSpring.position = 1;
        }

        void Update(float dt) {
            BasicViewWeapon::Update(dt);

            recoilVerticalSpring.damping = Mix(16, 24, AimDownSightState);
            recoilBackSpring.damping = Mix(12, 20, AimDownSightState);
            recoilRotationSpring.damping = Mix(8, 16, AimDownSightState);

            recoilVerticalSpring.Update(dt);
            recoilBackSpring.Update(dt);
            recoilRotationSpring.Update(dt);

            horizontalSwingSpring.velocity = horizontalSwingSpring.velocity + swing.x * 60 * dt * 2;
            horizontalSwingSpring.Update(dt);
            verticalSwingSpring.velocity = verticalSwingSpring.velocity + swing.z * 60 * dt * 2;
            verticalSwingSpring.Update(dt);

            reloadPitchSpring.Update(dt);
            reloadRollSpring.Update(dt);
            reloadOffsetSpring.Update(dt);
            trueReloadProgress += dt/2.5;
            
            sprintSpring.Update(dt);
            raiseSpring.Update(dt);

            bool isSprintingActive;
            if (sprintState >= 1) {
                isSprintingActive = true;
            } else if (sprintState > lastSprintState) {
                isSprintingActive = true;
            } else if (sprintState < lastSprintState) {
                isSprintingActive = false;
            } else if (sprintState <= 0) {
                isSprintingActive = false;
            } else {
                isSprintingActive = false;
            }

            lastSprintState = sprintState;

            if (isSprintingActive) {
                sprintSpring.desired = 1;
            } else {
                sprintSpring.desired = 0;
            }

            bool isRaised;
            if (raiseState >= 1) {
                isRaised = true;
            } else if (raiseState > lastRaiseState) {
                isRaised = true;
            } else if (raiseState < lastRaiseState) {
                isRaised = false;
            } else if (raiseState <= 0) {
                isRaised = false;
            } else {
                isRaised = false;
            }

            lastRaiseState = raiseState;

            if (isRaised) {
                raiseSpring.desired = 0.0;
            } else {
                raiseSpring.desired = 1.0;
            }

            swingFromSpring = Vector3(horizontalSwingSpring.position, 0, verticalSwingSpring.position);
        }

        void WeaponFired(){
            BasicViewWeapon::WeaponFired();

            if(!IsMuted){
                Vector3 origin = Vector3(0.4f, -0.3f, 0.5f);
                AudioParam param;
                param.referenceDistance = 4.f;
                param.volume = 1.f;
                audioDevice.PlayLocal(fireFarSound, origin, param);
                param.referenceDistance = 1.f;
                audioDevice.PlayLocal(fireStereoSound, origin, param);

            	param.volume = 8.f * environmentRoom;
				if (environmentSize < 0.5f) {
					audioDevice.PlayLocal(fireSmallReverbSound, origin, param);
				} else {
					audioDevice.PlayLocal(fireLargeReverbSound, origin, param);
				}
            }

            recoilVerticalSpring.velocity = recoilVerticalSpring.velocity + 1.5;
            recoilBackSpring.velocity = recoilBackSpring.velocity + 1.5;
            recoilRotationSpring.velocity = recoilRotationSpring.velocity + GetRandom()*2-1;
        }

        void ReloadingWeapon() {
            magazineTouched.Reset();
            magazineRemoved.Reset();
            magazineInserted.Reset();
            chargingHandlePulled.Reset();

            if(!IsMuted){
                Vector3 origin = Vector3(0.4f, -0.3f, 0.5f);
                AudioParam param;
                param.volume = 0.5f;
                audioDevice.PlayLocal(reloadSound, origin, param);
            }
            
            trueReloadProgress = 0;
        }

        float GetZPos() {
            return 0.f - AimDownSightStateSmooth * 0.0220f;
        }

        // rotates gun matrix to ensure the sight is in
        // the center of screen (0, ?, 0).
        Matrix4 AdjustToAlignSight(Matrix4 mat, Vector3 sightPos, float fade) {
            Vector3 p = mat * sightPos;
            mat = CreateRotateMatrix(Vector3(0.f, 0.f, 1.f), atan(p.x / p.y) * fade) * mat;
            mat = CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), atan(p.z / p.y) * fade) * mat;
            return mat;
        }

        // redefined from BasicViewWeapon.as
        Matrix4 GetViewWeaponMatrix() { 
            Matrix4 mat;
            mat = CreateEulerAnglesMatrix(Vector3(0.2, -0.0, -0.8)*sprintSpring.position) * mat;
            mat = CreateTranslateMatrix(Vector3(0.0, -0.1, 0.05)*sprintSpring.position) * mat;
            
            // raise gun animation
            mat = CreateRotateMatrix(Vector3(0.0, 0.0, 1.0), raiseSpring.position * -1.3) * mat;
            mat = CreateRotateMatrix(Vector3(0.0, 1.0, 0.0), raiseSpring.position * -1.2) * mat;
            mat = CreateRotateMatrix(Vector3(1.0, 0.0, 0.0), raiseSpring.position * -1) * mat;
            mat = CreateTranslateMatrix(Vector3(0.1, -0.3, 0.8) * raiseSpring.position) * mat;

            float unSightState = SmoothStep(1.0-AimDownSightState);
            
            // recoil animation
            Vector3 recoilRot;
            Vector3 recoilOffset;
            recoilRot = Vector3(-2.5 * recoilVerticalSpring.position, 0.3 * recoilRotationSpring.position, 0.3 * recoilRotationSpring.position) * unSightState;
            recoilOffset = Vector3(0.0, 0.0, -0.1) * recoilVerticalSpring.position;
            recoilOffset = recoilOffset + Vector3(0.0, -1.2, 0) * recoilBackSpring.position;

            // No recoil when the player is aiming. Multiply by (1 - aimScopingState)
            mat = CreateEulerAnglesMatrix(recoilRot) * mat;
            mat = mat * CreateTranslateMatrix(recoilOffset);

            // Weapon offset that transitions between aiming and not aiming.
            mat = CreateTranslateMatrix( 
                                         Mix(
                                              Vector3(-0.13, 0.3,0.2),
                                              Vector3( 0.0, 0.2, -(-2.5 -pivot.z)*globalScale),
                                              AimDownSightStateSmooth
                                            )
                                        ) * mat; 

            // offset from when the player is walking
            // again, don't move the gun when the weapon is aimed
            mat = CreateTranslateMatrix(swing * Vector3(1.0, 0.5, 1.0) * unSightState) * mat;

            // twist the gun when strafing
            // don't rotate when scoped
            mat = mat * CreateEulerAnglesMatrix(Vector3(-2.0*swingFromSpring.z, 0, 2.0*swingFromSpring.x) * unSightState);
            mat = mat * CreateTranslateMatrix(Vector3(0.5*swingFromSpring.x, 0, 0.5*swingFromSpring.z));
            
            mat = AdjustToAlignSight(
                mat, 
                (frontSightAttachment-pivot + Vector3(0, 0, -2)) * globalScale, AimDownSightStateSmooth
            );

            mat = AdjustToReload(mat);

            return mat;
        }

        void Draw2D() {
            if (AimDownSightState < 0.8)
                BasicViewWeapon::Draw2D();
        }

        void AddToScene() {
            Matrix4 mat = CreateScaleMatrix(globalScale);
            mat = GetViewWeaponMatrix() * mat;

            bool reloading = IsReloading;
            float reload = trueReloadProgress;
            Vector3 leftHand, rightHand;

            leftHand = mat * GetLeftHandOffset();
            rightHand = mat * GetRightHandOffset();

            ModelRenderParam param;
            Matrix4 weapMatrix = eyeMatrix * mat;
            param.matrix = weapMatrix *
                CreateTranslateMatrix(-0.f, 0.f, 0.f);
            param.depthHack = true;
            renderer.AddModel(gunModel, param);

            // draw sights
            Matrix4 sightMat = weapMatrix;
            sightMat *= CreateTranslateMatrix(rearSightAttachment - pivot);
            sightMat *= CreateScaleMatrix(rearSightScale);
            param.matrix = sightMat;
            renderer.AddModel(rearSightModel, param);

            sightMat = weapMatrix;
            sightMat *= CreateTranslateMatrix(frontSightAttachment - pivot);
            sightMat *= CreateScaleMatrix(frontSightScale);
            param.matrix = sightMat;
            renderer.AddModel(frontSightModel, param); 

            // draw charging handle
            Matrix4 chargingMat = weapMatrix;
            chargingMat *= CreateTranslateMatrix(chargingHandleAttachment - pivot);
            if (!IsReloading) { 
                if (readyState < 1.0) {
                    if (readyState < 0.05) {
                        chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) * readyState/0.05);
                    } else if (readyState < 0.10) {
                        chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) );
                    } else if (readyState  < 0.15) {
                        chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) * (0.10-readyState)/0.05);
                    }
                }
            } else {
                if (trueReloadProgress < 0.82) {
                    chargingMat = chargingMat;
                } else if (trueReloadProgress < 0.87) {
                    chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) * (trueReloadProgress - 0.82)/0.05);
                } else if (trueReloadProgress < 0.89) {
                    chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) * (1 - (trueReloadProgress - 0.87)/0.02));
                }
            }
            chargingMat *= CreateScaleMatrix(chargingHandleScale);
            param.matrix = chargingMat;
            renderer.AddModel(chargingHandleModel, param);

            // magazine/reload action
            Matrix4 magazineMat = weapMatrix;
            // Not reloading.
            magazineMat *= CreateTranslateMatrix(GetMagazineOffset());
            magazineMat *= CreateScaleMatrix(magazineScale);
            // 15 degree tilt forward
            // magazineMat *= CreateEulerAnglesMatrix( Vector3(-0.2618, 0, 0) );
            // magazineMat = AdjustMagazineToReload(magazineMat, trueReloadProgress);

            param.matrix = magazineMat;
            renderer.AddModel(magazineModel, param);

            LeftHandPosition = leftHand;
            RightHandPosition = rightHand;

            // muzzle flash appears for at least two frames in order to hide screen tearing.
            if( readyState < 0.04 * (1/0.5) ) {
                renderer.ColorP = Vector4(1.0, 0.6, 0.4, 0.0);
                renderer.AddSprite( muzzleFlashes[GetRandom(muzzleFlashes.length)], weapMatrix*(Vector3(1.5, 124, 1.5)-pivot), 0.5+0.1*GetRandom() , 2.0*PiF*GetRandom());
            }
        }
    }

    IWeaponSkin@ CreateViewRifleSkin(Renderer@ r, AudioDevice@ dev) {
        return ViewRifleSkin(r, dev);
    }
}
