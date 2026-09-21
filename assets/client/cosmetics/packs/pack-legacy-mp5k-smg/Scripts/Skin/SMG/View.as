/*
 Copyright (c) 2013 yvt
 Modified by Paratrooper

 This file is part of OpenSpades.
 Modified by PTrooper
 Modified by Nuceto

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
		private Model@ Weapon;
		private Model@ chargingHandle;
		private Model@ FrontSight;
		private Model@ DotSight;
		private Model@ Scope;
		private Model@ Suppressor;
		private Model@ ironSight;
		
		private Model@ MagFull;
		private Model@ MagEmpty;
		
		private Image@ dotSightImg;
		
		private ConfigItem mp5_sp("mp5_sp", "1");
		private ConfigItem mp5_png("mp5_png", "1");
		private ConfigItem mp5_DotSight("mp5_dotsight", "1");
		private ConfigItem mp5_ironSight("mp5_ironSight", "0");
		private ConfigItem mp5_disablephys("mp5_disablephys", "0");


        private AudioChunk@[] fireSounds(4);
		private AudioChunk@[] spfireSounds(4);
        private AudioChunk@ fireFarSound;
        private AudioChunk@ reloadSound;

        // Constants
        // Pivots
        // Weapon
        private Vector3 pivot = Vector3(3.5, 35.0, 9.0);
		// Front Sight
        private Vector3 frontSightAttachment = Vector3(1.5, 94.0, -0.5);

        // Scale
        // Weapon and global scale multiplier.
        private float globalScale = 0.015;
       
        // A bunch of springs.
        private ViewSMGSpring recoilVerticalSpring = ViewSMGSpring(300, 24);
        private ViewSMGSpring recoilBackSpring = ViewSMGSpring(200, 16);
        private ViewSMGSpring recoilRotationSpring = ViewSMGSpring(100, 8);
        private ViewSMGSpring horizontalSwingSpring = ViewSMGSpring(100, 12);
        private ViewSMGSpring verticalSwingSpring = ViewSMGSpring(100, 12);
        private ViewSMGSpring reloadPitchSpring = ViewSMGSpring(150, 12, 0);
        private ViewSMGSpring reloadRollSpring = ViewSMGSpring(150, 16, 0);
		private ViewSMGSpring ZPitchSpring = ViewSMGSpring(150, 16, 0);
        private ViewSMGSpring reloadOffsetSpring = ViewSMGSpring(150, 12, 0);
        private ViewSMGSpring sprintSpring = ViewSMGSpring(100, 11, 0);
        private ViewSMGSpring raiseSpring = ViewSMGSpring(200, 20, 1);
        private Vector3 swingFromSpring = Vector3();

        // A bunch of events.
        private ViewSMGEvent magazineTouched = ViewSMGEvent();
        private ViewSMGEvent magazineRemoved = ViewSMGEvent();
        private ViewSMGEvent magazineInserted = ViewSMGEvent();
        private ViewSMGEvent chargingHandlePulled = ViewSMGEvent();
		private ViewSMGEvent Slap = ViewSMGEvent();

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
            if (reloadProgress < 0.25) {
                reloadPitchSpring.desired = 0;
                reloadRollSpring.desired = 0;
			
            } else if (reloadProgress < 0.70) {
				reloadPitchSpring.desired = 0.5;
                reloadRollSpring.desired = 0.4;

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
                reloadPitchSpring.velocity = -2;
            }

            if (magazineInserted.WasActivated()) {
                magazineInserted.Acknowledge();
                reloadPitchSpring.velocity = 4;
            }

            if (chargingHandlePulled.WasActivated()) {
                chargingHandlePulled.Acknowledge();
                reloadPitchSpring.velocity = 1;
				ZPitchSpring.velocity = 1.5;
                reloadOffsetSpring.velocity = 0.5;
            }
			
			if(reloadProgress > 0.82){
				Slap.Activate();
			}
			
			if (Slap.WasActivated()) {
                Slap.Acknowledge();
				reloadPitchSpring.velocity = -2;
				ZPitchSpring.velocity = -1.5;
			}
			
            mat *= CreateEulerAnglesMatrix( Vector3(0.0, 0.6, 0.0) * reloadRollSpring.position );//Y
            mat *= CreateEulerAnglesMatrix( Vector3(-1.0, 0.0, 0.0) * reloadPitchSpring.position );//X
			mat *= CreateEulerAnglesMatrix( Vector3(0.0, 0.0, -1.0) * ZPitchSpring.position );
            mat *= CreateTranslateMatrix( Vector3(0.0, -1, 0) * reloadOffsetSpring.position );//translate Y

            return mat;
        }

        Vector3 GetMagazineOffset() {
		    Vector3 magazineAttachment = Vector3(-7.5f, 99.f, 25.f);
            if (reloadProgress < 0.30) {
                return magazineAttachment - pivot;
            } else if (reloadProgress < 0.35) {
                magazineRemoved.Activate();
                return Mix(
                    magazineAttachment - pivot,
                    magazineAttachment - pivot + Vector3(0.0, -120, 140),
                    SmoothStep(Min(1.0, (reloadProgress-0.30) / 0.10))
                );
            } else if (reloadProgress < 0.50) {
                return magazineAttachment - pivot + Vector3(0.0, -120, 140);
            } else if (reloadProgress < 0.60) {
                return Mix(
                    magazineAttachment - pivot + Vector3(0.0, -120, 140),
                    magazineAttachment - pivot,
                    SmoothStep(Min(1.0, (reloadProgress-0.5) / 0.1))
                );
            } else {
                magazineInserted.Activate();
                return magazineAttachment - pivot;
            }
        }

        Vector3 GetLeftHandOffset() {
            Vector3 leftHandDefaultOffset = Vector3(5.0, 50.0, 12.0);
			Vector3 ChargingHandle = Vector3(12.0, 50.0, 3.0);
			Vector3 ChargingHandleBack = Vector3(12.0, 45.0, 2.0);
			Vector3 magazine = Vector3(11.0, -22.0, 0.0);
			Vector3 magazinePos = Vector3(8.0, 45.0, 15.0);
			Vector3 slapUp = Vector3(12.0, 45.0, -2.0);
			Vector3 slapDown = Vector3(12.0, 55.0, 8.0);
        
            if (reloadProgress < 0.10) {
                return Mix(
                    leftHandDefaultOffset-pivot,
                    ChargingHandle-pivot,
                    SmoothStep(Min(1.0, (reloadProgress) / 0.10))
                );
            }else if (reloadProgress < 0.2) {
				chargingHandlePulled.Activate();
                return Mix(
                    ChargingHandle-pivot,
                    ChargingHandleBack-pivot,
                    SmoothStep(Min(1.0, (reloadProgress-0.10) / 0.10))
                );
            }else if (reloadProgress < 0.30) {
                return Mix(
                    ChargingHandleBack-pivot,
                    magazinePos-pivot,
                    SmoothStep(Min(1.0, (reloadProgress-0.20) / 0.05))
                );
            }else if (reloadProgress < 0.35) {
                return Mix(
                    magazinePos-pivot,
                    (GetMagazineOffset()/2) + magazine,
                    SmoothStep(Min(1.0, (reloadProgress-0.30) / 0.10))
                );
			}else if (reloadProgress < 0.70) {
                magazineTouched.Activate();
                return (GetMagazineOffset()/2) + magazine;
				
			} else if (reloadProgress < 0.80) {
                return Mix(
                    (GetMagazineOffset()/2) + magazine,
                    slapUp-pivot,
                    SmoothStep(Min(1.0, (reloadProgress-0.7) / 0.10))
                );
			}else if (reloadProgress < 0.85) {
                return Mix(
                    slapUp-pivot,
                    slapDown-pivot,
                    SmoothStep(Min(1.0, (reloadProgress-0.8) / 0.05))
                );
			}else if (reloadProgress < 0.95) {
                return Mix(
                    slapDown-pivot,
                    leftHandDefaultOffset-pivot,
                    SmoothStep(Min(1.0, (reloadProgress-0.85) / 0.10))
                );
            }else {
                return leftHandDefaultOffset-pivot;
            }
        }

        Vector3 GetRightHandOffset() {
			return Vector3(1, 30.0, 12.0)-pivot;   
        }

        ViewSMGSkin(Renderer@ r, AudioDevice@ dev){
            super(r);
            @audioDevice = dev;
			@Weapon = renderer.RegisterModel("Models/Weapons/SMG/WeaponFP.kv6");
			@DotSight = renderer.RegisterModel("Models/Weapons/SMG/DotSight.kv6");
			@MagFull = renderer.RegisterModel("Models/Weapons/SMG/MagazineFull.kv6");
			@MagEmpty = renderer.RegisterModel("Models/Weapons/SMG/MagazineEmpty.kv6");
			@chargingHandle = renderer.RegisterModel("Models/Weapons/SMG/ChargingHandle.kv6");
			@FrontSight = renderer.RegisterModel("Models/Weapons/SMG/FrontSight.kv6");
			@Suppressor = renderer.RegisterModel("Models/Weapons/SMG/Suppressor.kv6");
			@ironSight = renderer.RegisterModel("Models/Weapons/SMG/ironSight.kv6");
			@Scope = renderer.RegisterModel("Models/Weapons/SMG/ScopeModel.kv6");

            @fireSounds[0] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2Local1.wav");
            @fireSounds[1] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2Local2.wav");
            @fireSounds[2] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2Local3.wav");
            @fireSounds[3] = dev.RegisterSound
                ("Sounds/Weapons/SMG/V2Local4.wav");
				
			@spfireSounds[0] = dev.RegisterSound
                ("Sounds/Weapons/SMG/spV2Local1.wav");
            @spfireSounds[1] = dev.RegisterSound
                ("Sounds/Weapons/SMG/spV2Local2.wav");
            @spfireSounds[2] = dev.RegisterSound
                ("Sounds/Weapons/SMG/spV2Local3.wav");
            @spfireSounds[3] = dev.RegisterSound
                ("Sounds/Weapons/SMG/spV2Local4.wav");
				
            @fireFarSound = dev.RegisterSound
                ("Sounds/Weapons/SMG/FireFar.opus");
            @reloadSound = dev.RegisterSound
                ("Sounds/Weapons/SMG/ReloadLocal.wav");
			
			@dotSightImg = renderer.RegisterImage("Gfx/RedDot.png");

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
			ZPitchSpring.Update(dt);
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
			
			if(mp5_disablephys.IntValue == 0){
				swingFromSpring = Vector3(horizontalSwingSpring.position, 0, verticalSwingSpring.position);
			}
        }

        void WeaponFired(){
            BasicViewWeapon::WeaponFired();

            if(!IsMuted){
                Vector3 origin = Vector3(0.4f, -0.3f, 0.5f);
                AudioParam param;
                param.volume = 8.f;
				if(mp5_sp.FloatValue > 0){
					audioDevice.PlayLocal(spfireSounds[GetRandom(spfireSounds.length)], origin, param);
				}else{
					audioDevice.PlayLocal(fireSounds[GetRandom(fireSounds.length)], origin, param);
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
			Slap.Reset();

            if(!IsMuted){
                Vector3 origin = Vector3(0.4f, -0.3f, 0.5f);
                AudioParam param;
                param.volume = 1.f;
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
			if(mp5_disablephys.IntValue < 1){
				mat = CreateEulerAnglesMatrix(Vector3(0.2, -0.0, -0.8)*sprintSpring.position) * mat;
				mat = CreateTranslateMatrix(Vector3(0.0, -0.1, 0.05)*sprintSpring.position) * mat;
			}else{
				if(sprintStateSmooth > 0.f) {
					mat = CreateRotateMatrix(Vector3(0.f, 1.f, 0.f),
						sprintStateSmooth * -0.1f) * mat;
					mat = CreateRotateMatrix(Vector3(1.f, 0.f, 0.f),
						sprintStateSmooth * 0.3f) * mat;
					mat = CreateRotateMatrix(Vector3(0.f, 0.f, 1.f),
						sprintStateSmooth * -0.65f) * mat;
					mat = CreateTranslateMatrix(Vector3(0.03f, -0.15f, 0.05f)
						* sprintStateSmooth)  * mat;
				}
			}
            
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
			if(AimDownSightState == 0){
				mat = mat * CreateTranslateMatrix(recoilOffset);
			}

            // Weapon offset that transitions between aiming and not aiming.
			if(mp5_DotSight.IntValue > 0){
				mat = CreateTranslateMatrix(Mix(Vector3(-0.13, 0.3,0.2),
                                            Vector3(0.0184f, 0.2f - recoilBackSpring.position, (21.f -pivot.z)*globalScale),
                                            AimDownSightStateSmooth)) * mat;
				mat = CreateEulerAnglesMatrix(Mix(Vector3(0, 0,0),
                                            Vector3(0, 0.02, 0),
                                            AimDownSightStateSmooth)) * mat;
            }else{
				mat = CreateTranslateMatrix(Mix(Vector3(-0.13, 0.3,0.2),
                                            Vector3(0.0196f, 0.2f - recoilBackSpring.position, (18.8f -pivot.z)*globalScale),
                                            AimDownSightStateSmooth)) * mat;
				mat = CreateEulerAnglesMatrix(Mix(Vector3(0, 0,0),
                                            Vector3(0, 0.01, 0),
                                            AimDownSightStateSmooth)) * mat;
			}
			
            // offset from when the player is walking
            // again, don't move the gun when the weapon is aimed
            mat = CreateTranslateMatrix(swing * Vector3(1.0, 0.5, 1.0) * unSightState) * mat;

            // twist the gun when strafing
            // don't rotate when scoped
			if(unSightState > 0){
				mat = mat * CreateEulerAnglesMatrix(Vector3(-1.0*swingFromSpring.z, 0, 1.0*swingFromSpring.x) * unSightState);
				mat = mat * CreateTranslateMatrix(Vector3(0.5*swingFromSpring.x, 0, 0.5*swingFromSpring.z) * unSightState);
			}
			
            if(mp5_DotSight.IntValue > 0){
				mat = AdjustToAlignSight(
					mat, 
					(frontSightAttachment-pivot + Vector3(0.8, 1, -2.4)) * globalScale, AimDownSightStateSmooth
				);
			}else if(mp5_ironSight.IntValue == 0){
				// Optical axis of the mounted scope, before the reticle fills the view.
				mat = AdjustToAlignSight(mat, Vector3(-1.25f, 9.7f, -11.82f) * globalScale,
					AimDownSightStateSmooth);
			}else{
				mat = AdjustToAlignSight(
					mat, 
					(frontSightAttachment-pivot + Vector3(0.75, 1, -0.8)) * globalScale, AimDownSightStateSmooth
				);
			}

            mat = AdjustToReload(mat);

            return mat;
        }

        void Draw2D() {
			if(AimDownSightState > 0.6){
				string str = "Gfx/default.png";
				switch(mp5_png.IntValue){
					case 0: str = "Gfx/default.png"; break;
					case 1: str = "Gfx/scope1.png"; break;
					case 2: str = "Gfx/scope2.png"; break;
					case 3: str = "Gfx/scope3.png"; break;
					default: str = "Gfx/scope1.png"; break;	
				}
				Image@ img = renderer.RegisterImage(str);
				float height = renderer.ScreenHeight;
				float width = height * (800.f / 600.f); 
				renderer.Color = (Vector4(1.f, 1.f, 1.f, 1.f));
				if(mp5_DotSight.IntValue < 1 && mp5_ironSight.IntValue < 1){
					renderer.DrawImage(img,
					AABB2((renderer.ScreenWidth - width) * 0.5f,
					(renderer.ScreenHeight - height) * 0.5f,
					width, height));
				}
				return;
			}else{
				BasicViewWeapon::Draw2D();
			}
		}

        void AddToScene() {

			if(AimDownSightStateSmooth > 0.8 && mp5_DotSight.IntValue == 0 && mp5_ironSight.IntValue == 0){
				LeftHandPosition = Vector3(1.f, 6.f, 10.f);
				RightHandPosition = Vector3(0.f, -8.f, 20.f);
				return;
			}
			
            Matrix4 mat = CreateScaleMatrix(globalScale);
            mat = GetViewWeaponMatrix() * mat;

            bool reloading = IsReloading;
            Vector3 leftHand, rightHand;

            leftHand = mat * GetLeftHandOffset();
            rightHand = mat * GetRightHandOffset();

            ModelRenderParam param;
			Matrix4 weapMatrix = eyeMatrix * mat;
			param.matrix = weapMatrix;
			param.depthHack = true;
			
			// draw weapon
			param.matrix = weapMatrix
				* CreateScaleMatrix(0.13f)
				* CreateTranslateMatrix(-10.f, -20.f, 10.f)
				* CreateEulerAnglesMatrix(Vector3(0.f, 1.55f, 0.f));
			renderer.AddModel(Weapon, param);

			// draw charginghandle
			if(reloadProgress < 0.10){
				param.matrix = weapMatrix * CreateScaleMatrix(0.13f) 
										  * CreateTranslateMatrix(Vector3(-7.f, 107.f, -61.f));
			
			}else if(reloadProgress < 0.82){
				param.matrix = weapMatrix * CreateScaleMatrix(0.13f)
				* CreateTranslateMatrix(Mix(
					Vector3(-7.f, 107.f, -61.f),
                    Vector3(-7.f, 80.f, -61.f),
                    SmoothStep(Min(1.0, (reloadProgress-0.10) / 0.10))
				))
				* CreateEulerAnglesMatrix(Mix(
					Vector3(0.f, 0.f, 0.f),
                    Vector3(0.f, 1.f, 0.f),
                    SmoothStep(Min(1.0, (reloadProgress-0.10) / 0.05))
				));
			}else{
				param.matrix = weapMatrix * CreateScaleMatrix(0.13f) 
										  * CreateTranslateMatrix(Vector3(-7.f, 107.f, -61.f));
			}
			renderer.AddModel(chargingHandle, param);
			
			// draw frontsight
			param.matrix = weapMatrix
				* CreateScaleMatrix(0.065f)
				* CreateTranslateMatrix(-19.3f, 248.5f, -133.f);
			renderer.AddModel(FrontSight, param);
			
			// draw dotsight
			param.matrix = weapMatrix
				* CreateScaleMatrix(0.065f)
				* CreateTranslateMatrix(-18.f, 40.f, -145.f)
				* CreateEulerAnglesMatrix(Vector3(0.f, 1.55f, 0.f));
				
			if(mp5_DotSight.FloatValue > 0){
				renderer.AddModel(DotSight, param);
			}
			if(mp5_DotSight.IntValue == 0 && mp5_ironSight.IntValue == 0){
				// Nuceto's existing Kar98 scope, including its mount, fitted to this receiver.
				param.matrix = weapMatrix * CreateTranslateMatrix(-1.085f, 4.f, -9.4f)
					* CreateScaleMatrix(0.11f);
				renderer.AddModel(Scope, param);
			}
			
			// draw magazine
			param.matrix = weapMatrix
				* CreateScaleMatrix(0.13f)
				* CreateTranslateMatrix(GetMagazineOffset())
				* CreateEulerAnglesMatrix(Vector3(0.f, 1.55f, 0.f));
			
			if (reloadProgress < 0.4){
			
				renderer.AddModel(MagEmpty, param);
			} else {
			
				renderer.AddModel(MagFull, param);
			}
			
			// draw suppressor
			param.matrix = weapMatrix
				* CreateScaleMatrix(0.13f)
				* CreateTranslateMatrix(-10.f, 130.f, -44.f);
				
			if(mp5_sp.FloatValue > 0){
				renderer.AddModel(Suppressor, param);
			}
			
			// draw ironSight
			param.matrix = weapMatrix
				* CreateScaleMatrix(0.065f)
				* CreateTranslateMatrix(-19.5f, -35.f, -120.5f);
				
			if(mp5_ironSight.FloatValue > 0 && mp5_DotSight.FloatValue < 1){
				renderer.AddModel(ironSight, param);
			}
			
			if(mp5_DotSight.FloatValue > 0){
				renderer.AddSprite(dotSightImg, weapMatrix*(Vector3(2.28f, 39.f, -2.98f)-pivot), 0.11f, 0.f);
			}

            LeftHandPosition = leftHand;
            RightHandPosition = rightHand;                
        }
    }

    IWeaponSkin@ CreateViewSMGSkin(Renderer@ r, AudioDevice@ dev) {
        return ViewSMGSkin(r, dev);
    }
}
