/*
 Copyright (c) 2013 yvt

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
	class ViewRifleSkin:
	IToolSkin, IViewToolSkin, IWeaponSkin,
	BasicViewWeapon {

			// pivot of the weapon when viewed in slab6
		private Vector3 pivot = Vector3(-15.50, 33.0, 50.0);
		// scale of the weapon
		private float globalScale = 0.01;
		// scale of the magazine, relative to the global scale
		private float magazineScale = 0.5;
		// delta time between each frame
		private float deltatime = 0.0;
		// checks if mag has been thrown
		// reset to false whenever reloading
		private bool hasThrownMag = false;
		
		// Creates a rotation matrix from euler angles (in the form of a Vector3) x-y-z
		Matrix4 CreateEulerAnglesMatrix( Vector3 angles ) {
			Matrix4 mat = CreateRotateMatrix( Vector3(1.0, 0.0, 0.0), angles.x );
			mat = CreateRotateMatrix( Vector3(0.0, 1.0, 0.0), angles.y ) * mat;
			mat = CreateRotateMatrix( Vector3(0.0, 0.0, 1.0), angles.z ) * mat;
			
			return mat;
		}
	
		// select easing functions
		float quadraticIn(float per) {
			return (per*per);
		}
		
		float quadraticOut(float per) {
			return -(per * (per-2));
		}
		
		float quadraticInOut(float per) {
			per < per/2; 
			return (per*per);
			
			per > per/2; 
			return -(per * (per-2));
			
		}	
		
		float cubicIn(float per) {
			return (per*per*per);
		}
		
		float cubicOut(float per) {
			per = per-1;
			return (per*per*per + 1);
		}

		private AudioDevice@ audioDevice;
		private Model@ Base1;
		private Model@ NoMag;
		private Model@ NoMag1;
		private Model@ Base4;
		private Model@ Base5;
		private Model@ magazineModel;
		private Model@ Bolt;
		private Model@ Bolt1;

		private AudioChunk@ fireSound;
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireSmallReverbSound;
		private AudioChunk@ fireLargeReverbSound;
		private AudioChunk@ reloadSound;


		ViewRifleSkin(Renderer@ r, AudioDevice@ dev){
			super(r);
			@audioDevice = dev;
			@Base1 = renderer.RegisterModel
				("Models/Weapons/Rifle/Base1.kv6");
			@NoMag = renderer.RegisterModel
				("Models/Weapons/Rifle/WeaponNoMagazine.kv6");
			@NoMag1 = renderer.RegisterModel
				("Models/Weapons/Rifle/WeaponNoMagazine1.kv6");
			@Base4 = renderer.RegisterModel
				("Models/Weapons/Rifle/Base4.kv6");
			@Base5 = renderer.RegisterModel
				("Models/Weapons/Rifle/Base5.kv6");
			@magazineModel = renderer.RegisterModel
				("Models/Weapons/Rifle/Magazine.kv6");
			@Bolt = renderer.RegisterModel
				("Models/Weapons/Rifle/Bolt.kv6");
			@Bolt1 = renderer.RegisterModel
				("Models/Weapons/Rifle/Bolt1.kv6");

			@fireSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireLocal.wav");
			@fireFarSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireFar.wav");
			@reloadSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/ReloadLocal.wav");

			@fireSmallReverbSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/V2AmbienceSmall.opus");
			@fireLargeReverbSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/V2AmbienceLarge.opus");
		}

		void Update(float dt) {
			BasicViewWeapon::Update(dt);
		}

		void WeaponFired(){
			BasicViewWeapon::WeaponFired();

			if(!IsMuted){
				Vector3 origin = Vector3(0.4f, -0.3f, 0.5f);
				AudioParam param;
				param.volume = 8.f;
				audioDevice.PlayLocal(fireSound, origin, param);

				param.volume = 8.f * environmentRoom;
				if (environmentSize < 0.5f) {
					audioDevice.PlayLocal(fireSmallReverbSound, origin, param);
				} else {
					audioDevice.PlayLocal(fireLargeReverbSound, origin, param);
				}

				param.referenceDistance = 4.f;
				param.volume = 1.f;
				audioDevice.PlayLocal(fireFarSound, origin, param);
				param.referenceDistance = 1.f;
				audioDevice.PlayLocal(fireSound, origin, param);
			}
		}

		void ReloadingWeapon() {
			if(!IsMuted){
				Vector3 origin = Vector3(0.4f, -0.3f, 0.5f);
				AudioParam param;
				param.volume = 0.2f;
				audioDevice.PlayLocal(reloadSound, origin, param);
			}
		}

		float GetZPos() {
			return 0.2f - AimDownSightStateSmooth * 0.0520f;
		}

		// rotates gun matrix to ensure the sight is in
		// the center of screen (0, ?, 0).
		Matrix4 AdjustToAlignSight(Matrix4 mat, Vector3 sightPos, float fade) {
			Vector3 p = mat * sightPos;
			mat = CreateRotateMatrix(Vector3(0.f, 0.f, 1.f), atan(p.x / p.y) * fade) * mat;
			mat = CreateRotateMatrix(Vector3(-1.f, 0.f, 0.f), atan(p.z / p.y) * fade) * mat;
			return mat;
		}

		void Draw2D() {
			if(AimDownSightState > 0.6){
				Image@ img = renderer.RegisterImage("Gfx/riflesight.png");
				float height = renderer.ScreenHeight;
				float width = height * (1920.f / 1080.f); 
				renderer.Color = (Vector4(1.f, 1.f, 1.f, 1.f));
				renderer.DrawImage(img,
					AABB2((renderer.ScreenWidth - width) * 0.5f,
							(renderer.ScreenHeight - height) * 0.5f,
							width, height));
				return;
				}
			BasicViewWeapon::Draw2D();
		}

		// redefined from BasicViewWeapon.as
		Matrix4 GetViewWeaponMatrix() {	
			Matrix4 mat;
			// sprinting animation					
			if(sprintState > 0.0) {
				sprintState = quadraticIn(sprintState);
				mat = CreateEulerAnglesMatrix(Vector3(0.44, -0.0, -0.5)*sprintState) * mat;
				mat = CreateTranslateMatrix(Vector3(0.2, 0.0, 0.15)*sprintState) * mat;
			}
			
			// raise gun animation
			if(raiseState < 1.0) {
				float putdown = 1.0 - raiseState;
				putdown = cubicIn(putdown);
				mat = CreateRotateMatrix(Vector3(0.0, 0.0, 0.0),
					putdown * -1.3) * mat;
				mat = CreateRotateMatrix(Vector3(-1.0, 0.0, 0.0),
					putdown * 0.2) * mat;
				mat = CreateTranslateMatrix(Vector3(0.0, -0.3, 0.2)
					* putdown) * mat;
			}
			
			// recoil animation
			Vector3 recoilRot;
			Vector3 recoilOffset;
			if(readyState < 0.1) {
				float per = (readyState/0.1);
				per = cubicOut(per);
				recoilRot = Vector3(-0.1, 0.0, 0.0) * per;
				recoilOffset = Vector3(0.0, -0.08, -0.02) * per;
			} else if(readyState < 0.2) {
				recoilRot = Vector3(-0.1, 0.0, 0.0);
				recoilOffset = Vector3(0.0, -0.08, -0.02);
			} else if(readyState < 0.4) {
				float per = ( (readyState-0.2)/(0.4-0.2) );
				per = SmoothStep(per);
				recoilRot = Mix(Vector3(-0.1, 0.0, 0.0), Vector3(0.05, 0.0, 0.0), per);
				recoilOffset = Mix(Vector3(0.0, -0.08, -0.02), Vector3(0.0, 0.0, 0.0), per);
			} else if(readyState < 0.8) {
				float per = ( (readyState-0.4)/(0.8-0.4) );
				per = SmoothStep(per);
				recoilRot = Mix(Vector3(0.05, 0.0, 0.0), Vector3(0.0, 0.0, 0.0), per);
				recoilOffset = Vector3(0.0, 0.0, 0.0);
			}

			// No recoil when the player is aiming. Multiply by (1 - aimScopingState)
			float unSightState = 1.0-AimDownSightStateSmooth;
			mat = CreateEulerAnglesMatrix(recoilRot*unSightState) * mat;
			mat = mat * CreateTranslateMatrix(recoilOffset*unSightState);
			
			// default offset from when the player is not aiming (i.e. default position)
			mat = CreateTranslateMatrix( Mix( Vector3(-0.13, 0.5,0.2), Vector3(0.0, 0.05, -(0.-pivot.z)*globalScale), AimDownSightStateSmooth)) * mat; 

			// offset from when the player is walking
			// again, don't move the gun when the weapon is aimed
			mat = CreateTranslateMatrix(swing * GetMotionGain() * unSightState) * mat;

			// twist the gun when strafing
			// don't rotate when scoped
			mat = mat * CreateEulerAnglesMatrix(Vector3(3.0*swing.z, 3.0*swing.x, 0.0)*unSightState);
			
			return mat;
		}

		void AddToScene() {
			Matrix4 mat = CreateScaleMatrix(0.033f);
			mat = GetViewWeaponMatrix() * mat;

			bool reloading = IsReloading;
			float reload = ReloadProgress;
			Vector3 leftHand, rightHand;

			leftHand = mat * Vector3(0.f, 4.f, 2.2f);
			rightHand = mat * Vector3(-1.5f, -8.f, 2.f);

			Vector3 leftHand2 = mat * Vector3(5.f, -10.f, 4.f);
			Vector3 rightHand3 = mat * Vector3(-2.f, -7.f, -4.f);
			Vector3 rightHand4 = mat * Vector3(-3.f, -4.f, -6.f);

			if(AimDownSightStateSmooth > 0.8f){
				mat = AdjustToAlignSight(mat, Vector3(0.f, -50.f, -10.0f), (AimDownSightStateSmooth - 0.8f) / 0.2f);
			}

			ModelRenderParam param;
			Matrix4 weapMatrix = eyeMatrix * mat;
			Matrix4 magMatrix = eyeMatrix * mat;
			Matrix4 boltMatrix = eyeMatrix * mat;

			param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f) *
				CreateTranslateMatrix(1.f, 40.f, -45.f) * CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
			param.matrix = magMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f) *
				CreateTranslateMatrix(1.f, 40.f, -45.f) * CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
			param.matrix = boltMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f) *
				CreateTranslateMatrix(1.f, 40.f, -45.f) * CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));

			param.depthHack = true;

			if (reload < 1.0){ 
				if (reload < 0.15){  //weapon rotates a bit, hand to mag, right hand dissapears
					float per = ((reload-0.0) / (0.15-0.0));
					per = SmoothStep(per);

					leftHand = mat * (Mix(Vector3(0.f, 4.f, 2.2f), Vector3(0.f, -3.5f, 2.2f), per));
					rightHand = mat * (Mix(Vector3(-1.5f, -8.f, 2.f), Vector3(-30.f, -5.f, 30.f), per));

					mat = mat * CreateEulerAnglesMatrix (Mix(Vector3(0.0, 0.0, 0.0), Vector3(-0.5, 0.5, -0.8), per))
					* CreateTranslateMatrix (Mix(Vector3(0.0, 0.0, 0.0), Vector3(0.0, 1.0, 0.0), per));

					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f) 
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Base1, param);
					renderer.AddModel(NoMag, param);
					renderer.AddModel(NoMag1, param);
					renderer.AddModel(Base4, param);
					renderer.AddModel(Base5, param);
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);
					renderer.AddModel(magazineModel, param);
				}
				
				else if (reload < 0.20){  //jumps because of mag out
					float per = ((reload-0.15) / (0.20-0.15));
					per = SmoothStep(per);

					leftHand = mat * (Mix(Vector3(0.f, -3.5f, 2.2f), Vector3(0.f, -3.5f, 2.2f), per));
					rightHand = mat * (Mix(Vector3(-30.f, -5.f, 30.f), Vector3(-30.f, -5.f, 30.f), per));
 
					mat = mat * CreateEulerAnglesMatrix (Mix(Vector3(-0.5, 0.5, -0.8), Vector3(-0.5, 0.5, -1.0), per))
					* CreateTranslateMatrix (Mix(Vector3(0.0, 1.0, 0.0), Vector3(0.0, 1.0, 0.0), per));

					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f) 
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Base1, param);
					renderer.AddModel(NoMag, param);
					renderer.AddModel(NoMag1, param);
					renderer.AddModel(Base4, param);
					renderer.AddModel(Base5, param);
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);
					renderer.AddModel(magazineModel, param);
				}

				else if (reload < 0.27){  //Mag out
					float per = ((reload-0.15) / (0.27-0.15));
					per = quadraticInOut(per);

					leftHand = mat * (Mix(Vector3(0.f, -3.5f, 2.2f), Vector3(0.f, -3.5f, 18.f), per));
					rightHand = mat * (Mix(Vector3(-30.f, -5.f, 30.f), Vector3(-30.f, -5.f, 30.f), per));
 
					mat = mat * CreateEulerAnglesMatrix (Mix(Vector3(-0.5, 0.5, -1.0), Vector3(-0.5, 0.5, -0.8), per))
					* CreateTranslateMatrix (Mix(Vector3(0.0, 1.0, 0.0), Vector3(0.0, 1.0, 0.0), per));

					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f) 
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Base1, param);
					renderer.AddModel(NoMag, param);
					renderer.AddModel(NoMag1, param);
					renderer.AddModel(Base4, param);
					renderer.AddModel(Base5, param);
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);

					magMatrix = eyeMatrix * mat;
						param.matrix = magMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f) 
						* CreateTranslateMatrix (Mix(Vector3(1.0, 40.0, -45.0), Vector3(1.0, 40.0, 180.0), per))
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(magazineModel, param);
				}

				else if (reload < 0.43){  //Mag in
					float per = ((reload-0.27) / (0.43-0.27));
					per = quadraticInOut(per);

					leftHand = mat * (Mix(Vector3(0.f, -3.5f, 18.f), Vector3(0.f, -3.5f, 2.2f), per));
					rightHand = mat * (Mix(Vector3(-30.f, -5.f, 30.f), Vector3(-30.f, -5.f, 30.f), per));
 
					mat = mat * CreateEulerAnglesMatrix (Mix(Vector3(-0.5, 0.5, -0.8), Vector3(-0.5, 0.5, -0.8), per))
					* CreateTranslateMatrix (Mix(Vector3(0.0, 1.0, 0.0), Vector3(0.0, 1.0, 0.0), per));

					

					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f) 
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Base1, param);
					renderer.AddModel(NoMag, param);
					renderer.AddModel(NoMag1, param);
					renderer.AddModel(Base4, param);
					renderer.AddModel(Base5, param);
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);

				} else if (reload < 0.46){  //Jump because of Mag in
					float per = ((reload-0.43) / (0.46-0.43));
					per = quadraticInOut(per);

					leftHand = mat * (Mix(Vector3(0.f, -3.5f, 2.2f), Vector3(0.f, -3.5f, 2.2f), per));
					rightHand = mat * (Mix(Vector3(-30.f, -5.f, 30.f), Vector3(-30.f, -5.f, 30.f), per));
 
					mat = mat * CreateEulerAnglesMatrix (Mix(Vector3(-0.5, 0.5, -0.8), Vector3(-0.5, 0.5, -0.8), per))
					* CreateTranslateMatrix (Mix(Vector3(0.0, 1.0, 0.0), Vector3(0.0, 1.0, -0.5), per));

					

					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f) 
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Base1, param);
					renderer.AddModel(NoMag, param);
					renderer.AddModel(NoMag1, param);
					renderer.AddModel(Base4, param);
					renderer.AddModel(Base5, param);
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);

					magMatrix = eyeMatrix * mat;
						param.matrix = magMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f) 
						* CreateTranslateMatrix (Mix(Vector3(1.0, 40.0, 180.0), Vector3(1.0, 40.0, -45.0), per))
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(magazineModel, param);
				}

				else if (reload < 0.49){  //Keep Still
					float per = ((reload-0.46) / (0.49-0.46));
					per = quadraticInOut(per);

					leftHand = mat * (Mix(Vector3(0.f, -3.5f, 2.2f), Vector3(0.f, -3.5f, 2.2f), per));
					rightHand = mat * (Mix(Vector3(-30.f, -5.f, 30.f), Vector3(-30.f, -5.f, 30.f), per));
 
					mat = mat * CreateEulerAnglesMatrix (Mix(Vector3(-0.5, 0.5, -0.8), Vector3(-0.5, 0.5, -0.8), per))
					* CreateTranslateMatrix (Mix(Vector3(0.0, 1.0, -0.5), Vector3(0.0, 1.0, 0.0), per));

					

					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f) 
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Base1, param);
					renderer.AddModel(NoMag, param);
					renderer.AddModel(NoMag1, param);
					renderer.AddModel(Base4, param);
					renderer.AddModel(Base5, param);
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);

					magMatrix = eyeMatrix * mat;
						param.matrix = magMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f) 
						* CreateTranslateMatrix (Mix(Vector3(1.0, 40.0, -45.0), Vector3(1.0, 40.0, -45.0), per))
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(magazineModel, param);
				}

				else if (reload < 0.55){  //Hand goes down a bit
					float per = ((reload-0.49) / (0.55-0.49));
					per = quadraticInOut(per);

					leftHand = mat * (Mix(Vector3(0.f, -3.5f, 2.2f), Vector3(0.f, -3.5f, 7.f), per));
					rightHand = mat * (Mix(Vector3(-30.f, -5.f, 30.f), Vector3(-30.f, -5.f, 30.f), per));
 
					mat = mat * CreateEulerAnglesMatrix (Mix(Vector3(-0.5, 0.5, -0.8), Vector3(-0.5, 0.5, -0.8), per))
					* CreateTranslateMatrix (Mix(Vector3(0.0, 1.0, 0.0), Vector3(0.0, 1.0, 0.0), per));

					

					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f) 
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Base1, param);
					renderer.AddModel(NoMag, param);
					renderer.AddModel(NoMag1, param);
					renderer.AddModel(Base4, param);
					renderer.AddModel(Base5, param);
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);

					magMatrix = eyeMatrix * mat;
						param.matrix = magMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f) 
						* CreateTranslateMatrix (Mix(Vector3(1.0, 40.0, -45.0), Vector3(1.0, 40.0, -45.0), per))
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(magazineModel, param);
				}

				else if (reload < 0.66){  //Pushing mag again
					float per = ((reload-0.55) / (0.66-0.55));
					per = quadraticInOut(per);

					leftHand = mat * (Mix(Vector3(0.f, -3.5f, 7.f), Vector3(0.f, -3.5f, 2.2f), per));
					rightHand = mat * (Mix(Vector3(-30.f, -5.f, 30.f), Vector3(-30.f, -5.f, 30.f), per));
 
					mat = mat * CreateEulerAnglesMatrix (Mix(Vector3(-0.5, 0.5, -0.8), Vector3(-0.5, 0.5, -0.8), per))
					* CreateTranslateMatrix (Mix(Vector3(0.0, 1.0, 0.0), Vector3(0.0, 1.0, 0.0), per));

					

					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f) 
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Base1, param);
					renderer.AddModel(NoMag, param);
					renderer.AddModel(NoMag1, param);
					renderer.AddModel(Base4, param);
					renderer.AddModel(Base5, param);
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);

					magMatrix = eyeMatrix * mat;
						param.matrix = magMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f) 
						* CreateTranslateMatrix (Mix(Vector3(1.0, 40.0, -45.0), Vector3(1.0, 40.0, -45.0), per))
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(magazineModel, param);
				}


				else if (reload < 0.69){  // Bounce
					float per = ((reload-0.66) / (0.69-0.66));
					per = quadraticInOut(per);

					leftHand = mat * (Mix(Vector3(0.f, -3.5f, 2.2f), Vector3(0.f, -3.5f, 2.2f), per));
					rightHand = mat * (Mix(Vector3(-30.f, -5.f, 30.f), Vector3(-30.f, -5.f, 30.f), per));
 
					mat = mat * CreateEulerAnglesMatrix (Mix(Vector3(-0.5, 0.5, -0.8), Vector3(-0.5, 0.5, -0.8), per))
					* CreateTranslateMatrix (Mix(Vector3(0.0, 1.0, 0.0), Vector3(0.0, 1.0, -0.5), per));

					

					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f) 
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Base1, param);
					renderer.AddModel(NoMag, param);
					renderer.AddModel(NoMag1, param);
					renderer.AddModel(Base4, param);
					renderer.AddModel(Base5, param);
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);

					magMatrix = eyeMatrix * mat;
						param.matrix = magMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f) 
						* CreateTranslateMatrix (Mix(Vector3(1.0, 40.0, -45.0), Vector3(1.0, 40.0, -45.0), per))
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(magazineModel, param);
				}

				else if (reload < 0.76){  //Rotate weapon to original pos, hand goes original pos
					float per = ((reload-0.69) / (0.76-0.69));
					per = quadraticInOut(per);

					leftHand = mat * (Mix(Vector3(0.f, -3.5f, 2.2f), Vector3(0.f, 4.f, 2.2f), per));
					rightHand = mat * (Mix(Vector3(-30.f, -5.f, 30.f), Vector3(-1.5f, -8.f, 2.f), per));
 
					mat = mat * CreateEulerAnglesMatrix (Mix(Vector3(-0.5, 0.5, -0.8), Vector3(0.0, 0.0, 0.0), per))
					* CreateTranslateMatrix (Mix(Vector3(0.0, 1.0, -0.5), Vector3(0.0, 0.0, 0.0), per));

					

					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f) 
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Base1, param);
					renderer.AddModel(NoMag, param);
					renderer.AddModel(NoMag1, param);
					renderer.AddModel(Base4, param);
					renderer.AddModel(Base5, param);
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);

					magMatrix = eyeMatrix * mat;
						param.matrix = magMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f) 
						* CreateTranslateMatrix (Mix(Vector3(1.0, 40.0, -45.0), Vector3(1.0, 40.0, -45.0), per))
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(magazineModel, param);
				}

				else if (reload < 0.81){  //Hand to bolt
					float per = ((reload-0.76) / (0.81-0.76));
					per = quadraticInOut(per);

					leftHand = mat * (Mix(Vector3(0.f, 4.f, 2.2f), Vector3(0.f, 4.f, 2.2f), per));
					rightHand = mat * (Mix(Vector3(-1.5f, -8.f, 2.f), Vector3(-1.5f, -4.f, -3.f), per));
 
					mat = mat * CreateEulerAnglesMatrix (Mix(Vector3(0.0, 0.0, 0.0), Vector3(0.0, 0.0, 0.0), per))
					* CreateTranslateMatrix (Mix(Vector3(0.0, 0.0, 0.0), Vector3(0.0, 0.0, 0.0), per));

					

					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f) 
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Base1, param);
					renderer.AddModel(NoMag, param);
					renderer.AddModel(NoMag1, param);
					renderer.AddModel(Base4, param);
					renderer.AddModel(Base5, param);
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);

					magMatrix = eyeMatrix * mat;
						param.matrix = magMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f) 
						* CreateTranslateMatrix (Mix(Vector3(1.0, 40.0, -45.0), Vector3(1.0, 40.0, -45.0), per))
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(magazineModel, param);
				}

				else if (reload < 0.88){  //Bolt back
					float per = ((reload-0.81) / (0.88-0.81));
					per = quadraticInOut(per);

					leftHand = mat * (Mix(Vector3(0.f, 4.f, 2.2f), Vector3(0.f, 4.f, 2.2f), per));
					rightHand = mat * (Mix(Vector3(-1.5f, -4.f, -3.f), Vector3(-1.5f, -8.f, -3.f), per));
 
					mat = mat * CreateEulerAnglesMatrix (Mix(Vector3(0.0, 0.0, 0.0), Vector3(0.0, 0.0, 0.0), per))
					* CreateTranslateMatrix (Mix(Vector3(0.0, 0.0, 0.0), Vector3(0.0, -0.3, 0.0), per));

					

					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f) 
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Base1, param);
					renderer.AddModel(NoMag, param);
					renderer.AddModel(NoMag1, param);
					renderer.AddModel(Base4, param);
					renderer.AddModel(Base5, param);

					boltMatrix = eyeMatrix * mat;
						param.matrix = magMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f) 
						* CreateTranslateMatrix (Mix(Vector3(1.0, 40.0, -45.0), Vector3(1.0, 15.0, -45.0), per))
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);

					magMatrix = eyeMatrix * mat;
						param.matrix = magMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f) 
						* CreateTranslateMatrix (Mix(Vector3(1.0, 40.0, -45.0), Vector3(1.0, 40.0, -45.0), per))
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(magazineModel, param);
				}

				else if (reload < 0.95){  //Bolt return
					float per = ((reload-0.88) / (0.95-0.88));
					per = quadraticInOut(per);

					leftHand = mat * (Mix(Vector3(0.f, 4.f, 2.2f), Vector3(0.f, 4.f, 2.2f), per));
					rightHand = mat * (Mix(Vector3(-1.5f, -8.f, -3.f), Vector3(-1.5f, -4.f, -3.f), per));
 
					mat = mat * CreateEulerAnglesMatrix (Mix(Vector3(0.0, 0.0, 0.0), Vector3(0.0, 0.0, 0.0), per))
					* CreateTranslateMatrix (Mix(Vector3(0.0, -0.3, 0.0), Vector3(0.0, 0.0, 0.0), per));

					

					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f) 
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Base1, param);
					renderer.AddModel(NoMag, param);
					renderer.AddModel(NoMag1, param);
					renderer.AddModel(Base4, param);
					renderer.AddModel(Base5, param);

					boltMatrix = eyeMatrix * mat;
						param.matrix = magMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f) 
						* CreateTranslateMatrix (Mix(Vector3(1.0, 15.0, -45.0), Vector3(1.0, 40.0, -45.0), per))
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);

					magMatrix = eyeMatrix * mat;
						param.matrix = magMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f) 
						* CreateTranslateMatrix (Mix(Vector3(1.0, 40.0, -45.0), Vector3(1.0, 40.0, -45.0), per))
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(magazineModel, param);
				}

				else if (reload < 1.0){  //Hand to original pos
					float per = ((reload-0.95) / (1.0-0.95));
					per = quadraticInOut(per);

					leftHand = mat * (Mix(Vector3(0.f, 4.f, 2.2f), Vector3(0.f, 4.f, 2.2f), per));
					rightHand = mat * (Mix(Vector3(-1.5f, -4.f, -3.f), Vector3(-1.5f, -8.f, 2.f), per));
 
					mat = mat * CreateEulerAnglesMatrix (Mix(Vector3(0.0, 0.0, 0.0), Vector3(0.0, 0.0, 0.0), per))
					* CreateTranslateMatrix (Mix(Vector3(0.0, 0.0, 0.0), Vector3(0.0, 0.0, 0.0), per));

					

					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix * CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f) 
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Base1, param);
					renderer.AddModel(NoMag, param);
					renderer.AddModel(NoMag1, param);
					renderer.AddModel(Base4, param);
					renderer.AddModel(Base5, param);

					boltMatrix = eyeMatrix * mat;
						param.matrix = magMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f) 
						* CreateTranslateMatrix (Mix(Vector3(1.0, 40.0, -45.0), Vector3(1.0, 40.0, -45.0), per))
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);

					magMatrix = eyeMatrix * mat;
						param.matrix = magMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f) 
						* CreateTranslateMatrix (Mix(Vector3(1.0, 40.0, -45.0), Vector3(1.0, 40.0, -45.0), per))
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(magazineModel, param);
				}

			} 
			
			else { 
				(!reloading);
				renderer.AddModel(Base1, param);
				renderer.AddModel(NoMag, param);
				renderer.AddModel(NoMag1, param);
				renderer.AddModel(Base4, param);
				renderer.AddModel(Base5, param);
				renderer.AddModel(magazineModel, param);

				if (readyState > 1){
					weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f)
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
					renderer.AddModel(Bolt, param);
					renderer.AddModel(Bolt1, param);
				}

				else if (readyState < 0.25){ //Hand to bolt
					float per = ((readyState - 0.0)/(0.25-0.0));
						per = quadraticInOut(per);

						rightHand = mat * (Mix(Vector3(-1.5f, -8.f, 2.f),Vector3(-1.5f, -4.f, -3.f), per));

						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f)
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
						renderer.AddModel(Bolt, param);
						renderer.AddModel(Bolt1, param);

				}

				else if (readyState < 0.5){ //Bolt back, hand back
					float per = ((readyState - 0.25)/(0.5-0.25));
						per = quadraticInOut(per);

						rightHand = mat * (Mix(Vector3(-1.5f, -4.f, -3.f),Vector3(-1.5f, -8.f, -3.f), per));

						mat = mat * CreateTranslateMatrix (Mix(Vector3(0.0, 0.0, 0.0), Vector3(0.0, -1.6, 0.0), per));

						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f)
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
						renderer.AddModel(Bolt, param);
						renderer.AddModel(Bolt1, param);

				}

				else if (readyState < 0.75){ //Bolt return, hand return
					float per = ((readyState - 0.5)/(0.75-0.5));
						per = quadraticInOut(per);

						rightHand = mat * (Mix(Vector3(-1.5f, -8.f, -3.f),Vector3(-1.5f, -4.f, -3.f), per));

						mat = mat * CreateTranslateMatrix (Mix(Vector3(0.0, -1.6, 0.0), Vector3(0.0, 0.0, 0.0), per));

						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f)
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
						renderer.AddModel(Bolt, param);
						renderer.AddModel(Bolt1, param);

				}

				else if (readyState < 1.0){ //Original pos
					float per = ((readyState - 0.75)/(1.0-0.75));
						per = quadraticInOut(per);

						rightHand = mat * (Mix(Vector3(-1.5f, -4.f, -3.f),Vector3(-1.5f, -8.f, 2.f), per));

						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix
						* CreateScaleMatrix(0.06f, 0.06f, 0.064f)
						* CreateTranslateMatrix(1.f, 40.f, -45.f)
						* CreateEulerAnglesMatrix(Vector3(5.3f, 4.7f, 1.f));
						param.depthHack = true;
						renderer.AddModel(Bolt, param);
						renderer.AddModel(Bolt1, param);

				}
				}

			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
		}
	   }

	IWeaponSkin@ CreateViewRifleSkin(Renderer@ r, AudioDevice@ dev) {
		return ViewRifleSkin(r, dev);
	}
  }
