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
    class ViewSMGSpring {
        double position = 0; 
        double desired = 0;
        double velocity = 0;
        double frequency = 1;
        double damping = 1;
        
        ViewSMGSpring() {}

        ViewSMGSpring(double f, double d) {
            frequency = f;
            damping = d;
        }

        ViewSMGSpring(double f, double d, double des) {
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

    class ViewSMGEvent {
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

    class ViewSMGSkin:
    IToolSkin, IViewToolSkin, IWeaponSkin,
    BasicViewWeapon {

        private AudioDevice@ audioDevice;
        private Model@ gunModel;
        private Model@ magazineModel;
        private Model@ chargingHandleModel;
        private Model@ rearSightModel;
        private Model@ railsModel;

        private Image@[] muzzleFlashes(20);

        private AudioChunk@[] fireSounds(4);
        private AudioChunk@ fireFarSound;
        private AudioChunk@ fireStereoSound;
        private AudioChunk@[] fireSmallReverbSounds(4);
        private AudioChunk@[] fireLargeReverbSounds(4);
        private AudioChunk@ reloadSound;
        
        private Image@ redDot;

        // Constants
        // Pivots
        // Weapon
        private Vector3 pivot = Vector3(3.5, 35.0, 9.0);
        // Magazine
        private Vector3 magazinePivot = Vector3(1.5, 5.5, -0.5);
        // Charging Handle
        private Vector3 chargingHandlePivot = Vector3(9.5, -0.5, 3.5);

        // Attachment Points
        // Magazine
        private Vector3 magazineAttachment = Vector3(3.5, 42.5, 8.5);
        // Rear Sights
        private Vector3 rearSightAttachment = Vector3(3.5, 35, 1.5);
        // Front Sights
        private Vector3 frontSightAttachment = Vector3(1.5, 94.0, -0.5);
        // Charging Handle
        private Vector3 chargingHandleAttachment = Vector3(3.5, 54.5, 4);
        // Rails
        private Vector3 railsAttachment = Vector3(3.5, 32.5, 1.5);
        
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
        private float chargingHandleScale = 1;
        // Rails Scale
        private float railsScale = 0.5;

        // A bunch of springs.
        private ViewSMGSpring recoilVerticalSpring = ViewSMGSpring(300, 24);
        private ViewSMGSpring recoilBackSpring = ViewSMGSpring(200, 16);
        private ViewSMGSpring recoilRotationSpring = ViewSMGSpring(100, 8);
        private ViewSMGSpring horizontalSwingSpring = ViewSMGSpring(100, 12);
        private ViewSMGSpring verticalSwingSpring = ViewSMGSpring(100, 12);
        private ViewSMGSpring reloadPitchSpring = ViewSMGSpring(150, 12, 0);
        private ViewSMGSpring reloadRollSpring = ViewSMGSpring(150, 16, 0);
        private ViewSMGSpring reloadOffsetSpring = ViewSMGSpring(150, 12, 0);
        private ViewSMGSpring sprintSpring = ViewSMGSpring(100, 10, 0);
        private ViewSMGSpring raiseSpring = ViewSMGSpring(200, 20, 1);
        private Vector3 swingFromSpring = Vector3();

        // A bunch of events.
        private ViewSMGEvent magazineTouched = ViewSMGEvent();
        private ViewSMGEvent magazineRemoved = ViewSMGEvent();
        private ViewSMGEvent magazineInserted = ViewSMGEvent();
        private ViewSMGEvent chargingHandlePulled = ViewSMGEvent();

        // A bunch of states.
        private double lastSprintState = 0;
        private double lastRaiseState = 0;

        // Creates a rotation matrix from euler angles (in the form of a Vector3) x-y-z
        Matrix4 CreateEulerAnglesMatrix( Vector3 angles ) {
            Matrix4 mat = CreateRotateMatrix( Vector3(1.0, 0.0, 0.0), angles.x );
            mat = CreateRotateMatrix( Vector3(0.0, 1.0, 0.0), angles.y ) * mat;
            mat = CreateRotateMatrix( Vector3(0.0, 0.0, 1.0), angles.z ) * mat;
            
            return mat;
        }

        Matrix4 AdjustToReload(Matrix4 mat) {
            if (reloadProgress < 0.60) {
                reloadPitchSpring.desired = 0.6;
                reloadRollSpring.desired = 0.6;
            } else if (reloadProgress < 0.90) {
                reloadPitchSpring.desired = 0.6;
                reloadRollSpring.desired = -1.3;
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
            if (reloadProgress < 0.20) {
                return magazineAttachment - pivot;
            } else if (reloadProgress < 0.25) {
                magazineRemoved.Activate();
                return Mix(
                    magazineAttachment - pivot,
                    magazineAttachment - pivot + Vector3(0.0, -20.0, 50.0),
                    SmoothStep(Min(1.0, (reloadProgress-0.20) / 0.05))
                );
            } else if (reloadProgress < 0.40) {
                return magazineAttachment - pivot + Vector3(0.0, -20.0, 50.0);
            } else if (reloadProgress < 0.50) {
                return Mix(
                    magazineAttachment - pivot + Vector3(0.0, -20.0, 50.0),
                    magazineAttachment - pivot,
                    SmoothStep(Min(1.0, (reloadProgress-0.4) / 0.1))
                );
            } else {
                magazineInserted.Activate();
                return magazineAttachment - pivot;
            }
        }

        Vector3 GetLeftHandOffset() {
            Vector3 leftHandDefaultOffset = Vector3(6.0, 55.0, 12.0);
        
            if (reloadProgress < 0.10) {
                return Mix(
                    leftHandDefaultOffset-pivot,
                    GetMagazineOffset() + Vector3(0, 0, 15.0),
                    SmoothStep(Min(1.0, (reloadProgress) / 0.10))
                );
            } else if (reloadProgress < 0.60) {
                magazineTouched.Activate();
                return GetMagazineOffset() + Vector3(0, 0, 15.0);
            } else if (reloadProgress < 1.0) {
                return Mix(
                    GetMagazineOffset() + Vector3(0, 0, 15.0),
                    leftHandDefaultOffset-pivot,
                    SmoothStep(Min(1.0, (reloadProgress-0.60) / 0.10))
                );
            } else {
                return leftHandDefaultOffset-pivot;
            }
        }

        Vector3 GetRightHandOffset() {
            Vector3 chargingHandleOffset = chargingHandleAttachment-pivot + Vector3(-4, 8, 0);
            Vector3 rightHandDefaultOffset = Vector3(1, 30.0, 12.0);

            if (reloadProgress < 0.70) {
                return rightHandDefaultOffset-pivot;
            } else if (reloadProgress < 0.80) {
                return Mix(
                    rightHandDefaultOffset-pivot,
                    chargingHandleOffset,
                    SmoothStep(Min(1.0, (reloadProgress-0.70) / 0.10))
                );
            } else if (reloadProgress < 0.82) {
                return chargingHandleOffset;
            } else if (reloadProgress < 0.87) {
                chargingHandlePulled.Activate();
                return Mix(
                    chargingHandleOffset,
                    chargingHandleOffset + Vector3(0, -6, 0),
                    SmoothStep(Min(1.0, (reloadProgress-0.82) / 0.05))
                );
            } else if (reloadProgress < 1) {
                return Mix(
                    chargingHandleOffset + Vector3(0, -6, 0),
                    rightHandDefaultOffset-pivot,
                    SmoothStep(Min(1.0, (reloadProgress-0.87) / 0.13))
                );
            } else {
                return rightHandDefaultOffset-pivot;
            }
        }

        ViewSMGSkin(Renderer@ r, AudioDevice@ dev){
            super(r);
            @audioDevice = dev;
            @gunModel = renderer.RegisterModel
                ("Models/Weapons/SMG/WeaponNoMagazine.kv6");
            @magazineModel = renderer.RegisterModel
                ("Models/Weapons/SMG/Magazine.kv6");
            @rearSightModel = renderer.RegisterModel
                ("Models/Weapons/SMG/Scope.kv6");
            @chargingHandleModel = renderer.RegisterModel
                ("Models/Weapons/SMG/ChargingHandle.kv6");
            @railsModel = renderer.RegisterModel
                ("Models/Weapons/SMG/Rails.kv6");

            @fireSmallReverbSounds[0] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2AmbienceSmall1.opus");
            @fireSmallReverbSounds[1] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2AmbienceSmall2.opus");
            @fireSmallReverbSounds[2] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2AmbienceSmall3.opus");
            @fireSmallReverbSounds[3] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2AmbienceSmall4.opus");

            @fireLargeReverbSounds[0] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2AmbienceLarge1.opus");
            @fireLargeReverbSounds[1] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2AmbienceLarge2.opus");
            @fireLargeReverbSounds[2] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2AmbienceLarge3.opus");
            @fireLargeReverbSounds[3] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2AmbienceLarge4.opus");

            @fireSounds[0] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2Local1.opus");
            @fireSounds[1] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2Local2.opus");
            @fireSounds[2] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2Local3.opus");
            @fireSounds[3] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2Local4.opus");
            @fireFarSound = dev.RegisterSound
                ("Sounds/Weapons/SMG/FireFar.opus");
            @fireStereoSound = dev.RegisterSound
                ("Sounds/Weapons/SMG/FireStereo.opus");
            @reloadSound = dev.RegisterSound
                ("Sounds/Weapons/SMG/ReloadLocal.opus");

                
            @redDot = renderer.RegisterImage
                ( "Gfx/RedDot.png" );
                
            for ( uint i = 0; i < 20; i++ ) {
                string path = "Gfx/Flash/Weapons/SMG/";
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
                param.volume = 8.f;
                audioDevice.PlayLocal(fireSounds[GetRandom(fireSounds.length)], origin, param);

                param.volume = 8.f * environmentRoom;
                if (environmentSize < 0.5f) {
                    audioDevice.PlayLocal(fireSmallReverbSounds[GetRandom(fireSmallReverbSounds.length)], origin, param);
                } else {
                    audioDevice.PlayLocal(fireLargeReverbSounds[GetRandom(fireLargeReverbSounds.length)], origin, param);
                }
            }

            recoilVerticalSpring.velocity = recoilVerticalSpring.velocity + 0.75;
            recoilBackSpring.velocity = recoilBackSpring.velocity + 0.75;
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
                                              Vector3( 0.0, 0.27, -(-2.5 -pivot.z)*globalScale),
                                              AimDownSightStateSmooth
                                            )
                                        ) * mat; 

            // offset from when the player is walking
            // again, don't move the gun when the weapon is aimed
            mat = CreateTranslateMatrix(swing * Vector3(1.0, 0.5, 1.0) * unSightState) * mat;

            // twist the gun when strafing
            // don't rotate when scoped
            mat = mat * CreateEulerAnglesMatrix(Vector3(-2.0*swingFromSpring.z, 0, 2.0*swingFromSpring.x) * unSightState);
				mat = mat * CreateTranslateMatrix(Vector3(0.5*swingFromSpring.x, 0, 0.5*swingFromSpring.z) * unSightState);
            
            mat = AdjustToAlignSight(
                mat, 
                (rearSightAttachment-pivot + Vector3(0, 100, -4)) * globalScale, AimDownSightStateSmooth
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
            
            // draw rails
            Matrix4 railsMat = weapMatrix;
            railsMat *= CreateTranslateMatrix(railsAttachment - pivot);
            railsMat *= CreateScaleMatrix(railsScale);
            param.matrix = railsMat;
            renderer.AddModel(railsModel, param);

            // draw charging handle
            Matrix4 chargingMat = weapMatrix;
            chargingMat *= CreateTranslateMatrix(chargingHandleAttachment - pivot);
            chargingMat *= CreateScaleMatrix(chargingHandleScale);
            if (!IsReloading) { 
                if (readyState < 1.0) {
                    if (readyState < 0.20) {
                        chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) * readyState/0.20);
                    } else if (readyState < 0.40) {
                        chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) );
                    } else if (readyState  < 0.60) {
                        chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) * (1 - (readyState-0.40)/0.20));
                    }
                }
            } else {
                if (reloadProgress < 0.82) {
                    chargingMat = chargingMat;
                } else if (reloadProgress < 0.87) {
                    chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) * (reloadProgress - 0.82)/0.05);
                } else if (reloadProgress < 0.89) {
                    chargingMat *= CreateTranslateMatrix( Vector3(0, -6, 0) * (1 - (reloadProgress - 0.87)/0.02));
                }
            }
            param.matrix = chargingMat;
            renderer.AddModel(chargingHandleModel, param);

            // magazine/reload action
            Matrix4 magazineMat = weapMatrix;
            // Not reloading.
            magazineMat *= CreateTranslateMatrix(GetMagazineOffset());
            magazineMat *= CreateScaleMatrix(magazineScale);
            // 15 degree tilt forward
            // magazineMat *= CreateEulerAnglesMatrix( Vector3(-0.2618, 0, 0) );
            // magazineMat = AdjustMagazineToReload(magazineMat, reloadProgress);

            param.matrix = magazineMat;
            renderer.AddModel(magazineModel, param);

            LeftHandPosition = leftHand;
            RightHandPosition = rightHand;

            // muzzle flash appears for at least two frames in order to hide screen tearing.
            if( readyState < 0.04 * (1/0.1) ) {
                renderer.ColorP = Vector4(1.0, 0.6, 0.4, 0.0);
                renderer.AddSprite( muzzleFlashes[GetRandom(muzzleFlashes.length)], weapMatrix*(Vector3(3.5, 120, 6.5)-pivot), 0.5+0.1*GetRandom() , 2.0*PiF*GetRandom());
            }
            
            if (AimDownSightStateSmooth > 0.8 && !reloading) {
                ConfigItem r_renderer("r_renderer");
                renderer.ColorP = Vector4(1.0, 0.5, 0.5, 0.0);
                if ( r_renderer.StringValue == "sw" ) {
                    renderer.AddSprite(redDot, weapMatrix * (rearSightAttachment - pivot + Vector3(0, 30, -4)), 0.009f, PiF );
                } else {
                    renderer.AddSprite(redDot, weapMatrix * (rearSightAttachment - pivot + Vector3(0, 30, -4)), 0.09f, PiF );
                }
            }                
        }
    }

    IWeaponSkin@ CreateViewSMGSkin(Renderer@ r, AudioDevice@ dev) {
        return ViewSMGSkin(r, dev);
    }
}
