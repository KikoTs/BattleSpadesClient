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
	// A class for the magazine that gets thrown out when the rifle is reloaded
	class RifleMagazineParticle {
		private Matrix4 originalMatrix;
		private Vector3 worldVelocity;
		private Vector3 worldAcceleration;
		private Vector3 localEulerAngularVelocity; // don't use Euler angles, blah blah blah
		
		Matrix4 CreateEulerAnglesMatrix( Vector3 angles ) {
			Matrix4 mat = CreateRotateMatrix( Vector3(1.0, 0.0, 0.0), angles.x );
			mat = CreateRotateMatrix( Vector3(0.0, 1.0, 0.0), angles.y ) * mat;
			mat = CreateRotateMatrix( Vector3(0.0, 0.0, 1.0), angles.z ) * mat;
			
			return mat;
		}
		
		Matrix4 OriginalMatrix {
			set { originalMatrix = value; }
			get { return originalMatrix; }
		}
		
		Vector3 WorldVelocity {
			set { worldVelocity = value; }
			get { return worldVelocity; }
		}
		
		Vector3 WorldAcceleration {
			set { worldAcceleration = value; }
			get { return worldAcceleration; }
		}
		
		Vector3 LocalEulerAngularVelocity {
			set { localEulerAngularVelocity = value; }
			get { return localEulerAngularVelocity; }
		}
		
		private Renderer@ renderer;
		private Model@ objectModel;
		
		RifleMagazineParticle() {};
		RifleMagazineParticle(Renderer@ renderer) {
			@this.renderer = renderer;
			@objectModel = renderer.RegisterModel
				("Models/Weapons/Rifle/MagazineEmpty.kv6");
		}
		
		private float lifetime = 4.0;
		private float currentLife = 0.0;
		private bool isDead = true;
		
		bool IsDead {
			set { isDead = value; }
			get { return isDead; }
		}
		
		void Start(Matrix4 pos, Vector3 velocity, Vector3 acceleration, Vector3 angularv) {
			originalMatrix = pos;
			worldVelocity = velocity;
			worldAcceleration = acceleration;
			localEulerAngularVelocity = angularv;
			currentLife = 0.0;
			isDead = false;
		}
		
		void Update(float dt) {
			if(!isDead) {
				Matrix4 currentMatrix;
				currentMatrix = currentMatrix * CreateTranslateMatrix(0.0, 0.0, -10.0);
				currentMatrix = (CreateTranslateMatrix(worldVelocity*currentLife + worldAcceleration*currentLife*currentLife*0.5)) * originalMatrix;
				currentMatrix = currentMatrix * CreateEulerAnglesMatrix(localEulerAngularVelocity * currentLife);
			
				ModelRenderParam param;
				param.matrix = currentMatrix;
				param.depthHack = false;
				renderer.AddModel(objectModel, param);
				
				currentLife += dt;
				
				if(currentLife > lifetime) {
					isDead = true;
				}
				
				// don't let it fall off the map
				if(currentMatrix.GetOrigin().z > 63.0) {
					isDead = true;
				} 
			}
		}
	}
	
	// main weapon class
	class ViewRifleSkin: 
	IToolSkin, IViewToolSkin, IWeaponSkin,
	BasicViewWeapon {
		private AudioDevice@ audioDevice;
		private Model@ gunModel;
		private Model@ magazineEmptyModel;
		private Model@ magazineFullModel;
		private Model@ scopeModel;
		private Model@ singleVoxelModel;
		
		private Image@ smallCircleImage;
		private Image@[] muzzleFlashes(20);
		
		private AudioChunk@ fireSound;
		private AudioChunk@ fireFarSound;
		private AudioChunk@ fireStereoSound;
		private AudioChunk@ reloadSound;
		
		// pivot of the weapon when viewed in slab6
		private Vector3 pivot = Vector3(3.50, 33.0, 23.0);
		// scale of the weapon
		private float globalScale = 0.01;
		// scale of the magazine, relative to the global scale
		private float magazineScale = 0.5;
		// the magazine that gets thrown
		private RifleMagazineParticle mag;
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
		
		float cubicIn(float per) {
			return (per*per*per);
		}
		
		float cubicOut(float per) {
			per = per-1;
			return (per*per*per + 1);
		}
			
		ViewRifleSkin(Renderer@ r, AudioDevice@ dev){
			super(r);
			@audioDevice = dev;
			@gunModel = renderer.RegisterModel
				("Models/Weapons/Rifle/WeaponNoMagazine.kv6");
			@magazineEmptyModel = renderer.RegisterModel
				("Models/Weapons/Rifle/MagazineEmpty.kv6");
			@magazineFullModel = renderer.RegisterModel
				("Models/Weapons/Rifle/MagazineFull.kv6");
			@scopeModel = renderer.RegisterModel
				("Models/Weapons/Rifle/Scope.kv6");
			@singleVoxelModel = renderer.RegisterModel
				("Models/Weapons/Rifle/SingleVoxel.kv6");
			@smallCircleImage = renderer.RegisterImage
			 	("Gfx/SmallCircle.png");
			
			@fireSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireLocal.wav");
			@fireFarSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireFar.wav");
			@fireStereoSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/FireStereo.wav");
			@reloadSound = dev.RegisterSound
				("Sounds/Weapons/Rifle/ReloadLocal.wav");
				
			for ( uint i = 0; i < 20; i++ ) {
				string dir = "Gfx/Flash/Weapons/Rifle/";
				dir += i / 100; 			// hundreds
				dir += i % 100 / 10;		// tens
				dir += i % 10;				// units
				dir += ".png";
				@muzzleFlashes[i] = renderer.RegisterImage( dir );
			}
			
			mag = RifleMagazineParticle(renderer);
		}
		
		void Update(float dt) {			
			deltatime = dt;
			BasicViewWeapon::Update(dt);
		}
		
		void WeaponFired(){
			BasicViewWeapon::WeaponFired();
			
			if(!IsMuted){
				Vector3 origin = Vector3(0.4, -0.3, 0.5);
				AudioParam param;
				param.volume = 8.0;
				audioDevice.PlayLocal(fireSound, origin, param);
				
				param.referenceDistance = 4.0;
				param.volume = 1.0;
				audioDevice.PlayLocal(fireFarSound, origin, param);
				param.referenceDistance = 1.0;
				audioDevice.PlayLocal(fireStereoSound, origin, param);
			}
		}
		
		void ReloadingWeapon() {
			hasThrownMag = false;
			if(!IsMuted){
				Vector3 origin = Vector3(0.4, -0.3, 0.5);
				AudioParam param;
				param.volume = 0.2;
				audioDevice.PlayLocal(reloadSound, origin, param);
			}
		}
		
		// draw the 2D crosshairs
		void Draw2D() {
			ConfigItem r_renderer("r_renderer");
			// if we're NOT using the gl renderer, draw a ring
			if(r_renderer.StringValue != "gl" && AimDownSightStateSmooth > 0.99) {
				renderer.ColorNP = (Vector4(1.0, 1.0, 1.0, 1.0));
				renderer.DrawImage(smallCircleImage,
				Vector2((renderer.ScreenWidth-smallCircleImage.Width) * 0.5,
					(renderer.ScreenHeight-smallCircleImage.Height) * 0.5));
			}
			if(AimDownSightStateSmooth < 0.99) {
				BasicViewWeapon::Draw2D();
			}
		}
		
		// redefined from BasicViewWeapon.as
		Matrix4 GetViewWeaponMatrix() {	
			Matrix4 mat;
			// sprinting animation					
			if(sprintState > 0.0) {
				sprintState = quadraticIn(sprintState);
				mat = CreateEulerAnglesMatrix(Vector3(0.2, -0.0, -0.2)*sprintState) * mat;
				mat = CreateTranslateMatrix(Vector3(0.1, -0.2, 0.05)*sprintState) * mat;
			}
			
			// raise gun animation
			if(raiseState < 1.0) {
				float putdown = 1.0 - raiseState;
				putdown = cubicIn(putdown);
				mat = CreateRotateMatrix(Vector3(0.0, 0.0, 1.0),
					putdown * -1.3) * mat;
				mat = CreateRotateMatrix(Vector3(0.0, 1.0, 0.0),
					putdown * 0.2) * mat;
				mat = CreateTranslateMatrix(Vector3(0.1, -0.3, 0.8)
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
			mat = CreateTranslateMatrix( Mix( Vector3(-0.13, 0.3,0.2), Vector3(0.0, 0.05, -(4.5-pivot.z)*globalScale), AimDownSightStateSmooth)) * mat; 

			// offset from when the player is walking
			// again, don't move the gun when the weapon is aimed
			mat = CreateTranslateMatrix(swing * GetMotionGain() * unSightState) * mat;

			// twist the gun when strafing
			// don't rotate when scoped
			mat = mat * CreateEulerAnglesMatrix(Vector3(0.0, 2.0*swing.x, 0.0)*unSightState);
			
			return mat;
		}
		
		void AddToScene() {	
			Matrix4 mat = CreateScaleMatrix(globalScale);
			mat = GetViewWeaponMatrix() * mat;
			
			Vector3 leftHand, rightHand;
			
			leftHand = mat * (Vector3(4.5, 72.0, 30.0)-pivot);
			rightHand = mat * (Vector3(4.0, 37.0, 22.0)-pivot);
			
			Matrix4 weapMatrix;
			Matrix4 magazineMatrix;
			
			mag.Update(deltatime);
			if(AimDownSightStateSmooth < 0.99) { // if we're not scoped in, then
				// draw weapon
				ModelRenderParam param;
				param.depthHack = true;
				
				if(reloadProgress < 1.0) { // long reloading sequence ahead. please collapse.
					if(reloadProgress < 0.16) { // rotate gun clockwise and raise gun while left hand grabs mag
						float per = ( (reloadProgress-0.0)/(0.16-0.0) );
						per = quadraticOut(per);
						mat = mat * CreateEulerAnglesMatrix( Vector3(-0.5, 0.5, -0.8) * per );

						leftHand = mat * (Mix( Vector3(4.5, 72.0, 30.0), Vector3(20.0, 0.0, 70.0), per) - pivot);
						rightHand = mat * (Vector3(4.0, 37.0, 22.0) - pivot);
						
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix;
						renderer.AddModel(gunModel, param);
					
						magazineMatrix = weapMatrix
							* CreateTranslateMatrix( (Vector3(3.5, 55.5, 21.0)-pivot) )
							* CreateScaleMatrix(magazineScale);
						param.matrix = magazineMatrix;
						renderer.AddModel(magazineFullModel, param);
					} else if(reloadProgress < 0.24) { // jostle gun a bit
						float per = ( (reloadProgress-0.16)/(0.24-0.16) );
						per = SmoothStep(per);
						
						mat = mat * CreateEulerAnglesMatrix( Mix( Vector3(-0.5, 0.5, -0.8), Vector3(-0.45, 0.4, -0.8), per) );
						leftHand = mat * Vector3(20.0, 0.0, 70.0);
						rightHand = mat * (Vector3(4.0, 37.0, 22.0) - pivot);
						
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix;
						renderer.AddModel(gunModel, param);
					
						magazineMatrix = weapMatrix
							* CreateTranslateMatrix( (Vector3(3.5, 55.5, 21.0)-pivot) )
							* CreateScaleMatrix(magazineScale);
						param.matrix = magazineMatrix;
						renderer.AddModel(magazineFullModel, param);
					} else if(reloadProgress < 0.32) { // hit the magazine release
						float per = ( (reloadProgress-0.24)/(0.32-0.24) );
						per = quadraticOut(per);
						
						mat = mat * CreateEulerAnglesMatrix( Mix( Vector3(-0.45, 0.4, -0.8), Vector3(-0.4, 0.4, -0.8), per) );
						leftHand = mat * (Mix( Vector3(20.0, 0.0, 70.0), Vector3(9.5, 46.0, 33.0), per) - pivot);
						rightHand = mat * (Vector3(4.0, 37.0, 22.0) - pivot);

						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix;
						renderer.AddModel(gunModel, param);
					
						magazineMatrix = weapMatrix
							* CreateTranslateMatrix( (Vector3(3.5, 55.5, 21.0)-pivot) )
							* CreateScaleMatrix(magazineScale);
						param.matrix = magazineMatrix;
						renderer.AddModel(magazineFullModel, param);
						
						// temporary magazine attached to the left hand
						param.matrix = eyeMatrix 
							* mat 
							* CreateTranslateMatrix(Mix( Vector3(20.0, 0.0, 70.0), Vector3(9.5, 46.0, 33.0), per) - pivot) 
							* CreateTranslateMatrix(-2.0, 1.5, -15.0)
							* CreateScaleMatrix(magazineScale);
						renderer.AddModel(magazineFullModel, param);
					} else if(reloadProgress < 0.38) { // release mag and move hand down
						float per = ( (reloadProgress-0.32)/(0.38-0.32) );
						per = quadraticOut(per);
						
						mat = mat * CreateEulerAnglesMatrix( Mix( Vector3(-0.4, 0.4, -0.8), Vector3(-0.5, 0.45, -0.8), per) );
						leftHand = mat * (Mix( Vector3(9.5, 46.0, 33.0), Vector3(5.5, 50.0, 55.0), per) - pivot);
						rightHand = mat * (Vector3(4.0, 37.0, 22.0) - pivot);

						weapMatrix = eyeMatrix * mat;					
						param.matrix = weapMatrix;
						renderer.AddModel(gunModel, param);

						param.matrix = eyeMatrix 
							* mat 
							* CreateTranslateMatrix(Mix( Vector3(9.5, 46.0, 33.0), Vector3(5.5, 50.0, 55.0), per) - pivot) 
							* CreateTranslateMatrix(-2.0, 1.5, -15.0)
							* CreateScaleMatrix(magazineScale);
						renderer.AddModel(magazineFullModel, param);

						if(!hasThrownMag) {
							mag.Start(weapMatrix * CreateScaleMatrix(magazineScale) * CreateTranslateMatrix( (Vector3(3.5, 55.5, 21.0)-pivot) / magazineScale ),
								weapMatrix.GetAxis(1)*4.0/globalScale,
								Vector3(0.0, 0.0, 0.3)/globalScale,
								Vector3(-24.0, 0.0, 0.0));
							hasThrownMag = true;
						}
					} else if(reloadProgress < 0.44) {// shove the mag into the well
						float per = ( (reloadProgress-0.38)/(0.44-0.38) );
						per = quadraticIn(per);
						
						mat = mat * CreateEulerAnglesMatrix( Mix( Vector3(-0.5, 0.45, -0.8), Vector3(-0.45, 0.45, -0.8), per) );
						leftHand = mat * (Mix( Vector3(5.5, 50.0, 55.0), Vector3(5.5, 54.0, 37.0), per) - pivot);
						rightHand = mat * (Vector3(4.0, 37.0, 22.0) - pivot);
						
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix;
						renderer.AddModel(gunModel, param);
						
						param.matrix = eyeMatrix * mat 
							* CreateTranslateMatrix(Mix( Vector3(5.5, 50.0, 55.0), Vector3(5.5, 54.0, 37.0), per) - pivot) 
							* CreateTranslateMatrix(-2.0, 1.5, -15.0)
							* CreateScaleMatrix(magazineScale);
						renderer.AddModel(magazineFullModel, param);					
					} else if(reloadProgress < 0.5) { // lower hand as if trying to push mag farther, gun rises due to shove
						float per = ( (reloadProgress-0.44)/(0.5-0.44) );
						per = quadraticOut(per);
						
						mat = mat * CreateEulerAnglesMatrix( Mix( Vector3(-0.45, 0.45, -0.8), Vector3(-0.55, 0.45, -0.8), per) );
						leftHand = mat * (Mix( Vector3(5.5, 54.0, 42.0), Vector3(5.5, 54.0, 65.0), per) - pivot);
						rightHand = mat * (Vector3(4.0, 37.0, 22.0) - pivot);
						
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix;
						renderer.AddModel(gunModel, param);
					
						magazineMatrix = weapMatrix
							* CreateTranslateMatrix( (Vector3(3.5, 55.5, 21.0)-pivot) )
							* CreateScaleMatrix(magazineScale);
						param.matrix = magazineMatrix;
						renderer.AddModel(magazineFullModel, param);
					} else if(reloadProgress < 0.54) { // forcefully push mag, gun lowers a bit
						float per = ( (reloadProgress-0.50)/(0.54-0.50) );
						per = quadraticIn(per);
						
						mat = mat * CreateEulerAnglesMatrix( Mix( Vector3(-0.55, 0.45, -0.8), Vector3(-0.55, 0.45, -0.8), per) );
						leftHand = mat * (Mix( Vector3(5.5, 54.0, 65.0), Vector3(5.5, 54.0, 41.0), per) - pivot);
						rightHand = mat * (Vector3(4.0, 37.0, 22.0) - pivot);
						
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix;
						renderer.AddModel(gunModel, param);
					
						magazineMatrix = weapMatrix
							* CreateTranslateMatrix( (Vector3(3.5, 55.5, 21.0)-pivot) )
							* CreateScaleMatrix(magazineScale);
						param.matrix = magazineMatrix;
						renderer.AddModel(magazineFullModel, param);
					} else if(reloadProgress < 0.60) { // hold mag for a bit. Let gun rise due to force.
					float per = ( (reloadProgress-0.54)/(0.60-0.54) );
						per = quadraticOut(per);
						
						mat = mat * CreateEulerAnglesMatrix( Mix( Vector3(-0.55, 0.45, -0.8), Vector3(-0.8, 0.45, -0.8), per) );
						leftHand = mat * (Vector3(5.5, 54.0, 41.0) - pivot);
						rightHand = mat * (Vector3(4.0, 37.0, 22.0) - pivot);
						
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix;
						renderer.AddModel(gunModel, param);
					
						magazineMatrix = weapMatrix
							* CreateTranslateMatrix( (Vector3(3.5, 55.5, 21.0)-pivot) )
							* CreateScaleMatrix(magazineScale);
						param.matrix = magazineMatrix;
						renderer.AddModel(magazineFullModel, param);
					} else if(reloadProgress < 0.70) { // gun levels slightly. left arm moves to default position.
						float per = ( (reloadProgress-0.60)/(0.70-0.60) );
						per = SmoothStep(per);
						
						mat = mat * CreateEulerAnglesMatrix( Mix( Vector3(-0.8, 0.45, -0.8), Vector3(-0.2, 0.1, -0.4), per) );
						leftHand = mat * (Mix( Vector3(5.5, 54.0, 41.0), Vector3(4.5, 72.0, 30.0), per) - pivot);
						rightHand = mat * (Vector3(4.0, 37.0, 22.0) - pivot);
						
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix;
						renderer.AddModel(gunModel, param);
					
						magazineMatrix = weapMatrix
							* CreateTranslateMatrix( (Vector3(3.5, 55.5, 21.0)-pivot) )
							* CreateScaleMatrix(magazineScale);
						param.matrix = magazineMatrix;
						renderer.AddModel(magazineFullModel, param);
					} else if(reloadProgress < 0.78) { // move right arm to the charging handle
						float per = ( (reloadProgress-0.70)/(0.78-0.70) );
						per = quadraticOut(per);
						
						mat = mat * CreateEulerAnglesMatrix( Mix( Vector3(-0.2, 0.1, -0.4), Vector3(0.1, 0.1, 0.0), per) );
						leftHand = mat * (Vector3(4.5, 72.0, 30.0)-pivot);
						rightHand = mat * (Mix( Vector3(4.0, 37.0, 22.0), Vector3(-4.0, 80.0, 15.0), per) - pivot);
						
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix;
						renderer.AddModel(gunModel, param);
					
						magazineMatrix = weapMatrix
							* CreateTranslateMatrix( (Vector3(3.5, 55.5, 21.0)-pivot) )
							* CreateScaleMatrix(magazineScale);
						param.matrix = magazineMatrix;
						renderer.AddModel(magazineFullModel, param);
					} else if(reloadProgress < 0.86) { // pull the charging handle
						float per = ( (reloadProgress-0.78)/(0.86-0.78) );
						per = quadraticOut(per);
						
						mat = mat * CreateEulerAnglesMatrix( Mix( Vector3(0.1, 0.1, 0.0), Vector3(-0.2, 0.0, 0.0), per) );
						leftHand = mat * (Vector3(4.5, 72.0, 30.0)-pivot);
						rightHand = mat * (Mix( Vector3(-4.0, 80.0, 15.0), Vector3(-4.0, 60.0, 15.0), per) - pivot);
						
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix;
						renderer.AddModel(gunModel, param);
					
						magazineMatrix = weapMatrix
							* CreateTranslateMatrix( (Vector3(3.5, 55.5, 21.0)-pivot) )
							* CreateScaleMatrix(magazineScale);
						param.matrix = magazineMatrix;
						renderer.AddModel(magazineFullModel, param);
					} else if(reloadProgress < 0.9) { // wait for the charging handle to return
						float per = ( (reloadProgress-0.86)/(0.9-0.86) );
						per = quadraticOut(per);
						
						mat = mat * CreateEulerAnglesMatrix( Mix( Vector3(-0.2, 0.0, 0.0), Vector3(-0.15, 0.0, 0.0), per) );
						leftHand = mat * (Vector3(4.5, 72.0, 30.0)-pivot);
						rightHand = mat * (Mix( Vector3(-4.0, 60.0, 15.0), Vector3(-8.0, 60.0, 10.0), per) - pivot);
						
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix;
						renderer.AddModel(gunModel, param);
					
						magazineMatrix = weapMatrix
							* CreateTranslateMatrix( (Vector3(3.5, 55.5, 21.0)-pivot) )
							* CreateScaleMatrix(magazineScale);
						param.matrix = magazineMatrix;
						renderer.AddModel(magazineFullModel, param);
					} else if(reloadProgress < 1.0) { // right arm to original position
						float per = ( (reloadProgress-0.9)/(1.0-0.9) );
						per = quadraticOut(per);
						
						mat = mat * CreateEulerAnglesMatrix( Mix( Vector3(-0.15, 0.0, 0.0), Vector3(0.0, 0.0, 0.0), per) );
						leftHand = mat * (Vector3(4.5, 72.0, 30.0)-pivot);
						rightHand = mat * (Mix( Vector3(-8.0, 60.0, 10.0), Vector3(4.0, 37.0, 22.0), per) - pivot);
						
						weapMatrix = eyeMatrix * mat;
						param.matrix = weapMatrix;
						renderer.AddModel(gunModel, param);
					
						magazineMatrix = weapMatrix
							* CreateTranslateMatrix( (Vector3(3.5, 55.5, 21.0)-pivot) )
							* CreateScaleMatrix(magazineScale);
						param.matrix = magazineMatrix;
						renderer.AddModel(magazineFullModel, param);
					}
				} else { // if we're not reloading, then draw gun as normal
					weapMatrix = eyeMatrix * mat;
					param.matrix = weapMatrix;
					renderer.AddModel(gunModel, param);
					
					magazineMatrix = weapMatrix
						* CreateTranslateMatrix( (Vector3(3.5, 55.5, 21.0)-pivot) )
						* CreateScaleMatrix(magazineScale);
					param.matrix = magazineMatrix;
					renderer.AddModel(magazineFullModel, param);
				}
				
				LeftHandPosition = leftHand;
				RightHandPosition = rightHand;
			} else { // else if we are scoped in, then draw the scope (if we can)
				// hide the hands
				leftHand = Vector3(0.0, 0.0, 0.0);
				rightHand = Vector3(0.0, 0.0, 0.0);
				// Non-uniform scaling is not allowed when using the software renderer.
				// ONLY draw the crosshairs and scope if we're using the gl renderer. 
				// Otherwise, just draw an image in the center (check Draw2D).
				ConfigItem r_renderer("r_renderer");
				if(r_renderer.StringValue == "gl") {
					ModelRenderParam param;
					Matrix4 scopeMatrix = eyeMatrix * CreateScaleMatrix(0.01f) * CreateTranslateMatrix(Vector3(0.0, 50.0, 0.0));
					param.matrix = scopeMatrix;
					param.depthHack = true;
					renderer.AddModel(scopeModel, param);
				
					// vertical hair
					param.matrix = scopeMatrix 
						* CreateTranslateMatrix(0.0, 30.0, 0.0)
						* CreateScaleMatrix(0.1, 0.1, 40.0);
					renderer.AddModel(singleVoxelModel, param);	
					
					// horizontal hair
					param.matrix = scopeMatrix 
						* CreateTranslateMatrix(0.0, 30.0, 0.0)
						* CreateScaleMatrix(40.0, 0.1, 0.1);
					renderer.AddModel(singleVoxelModel, param);
				}
				LeftHandPosition = leftHand;
				RightHandPosition = rightHand;
			}
			
			// Muzzle flash
			// Only appears if we're not scoped in
			if(AimDownSightStateSmooth < 1.0) { 
				if( readyState < 0.04 * (1/0.5) ) { // muzzle flash appears for 0.06 seconds ( at least 2 frames @ 30 fps or 3 frames @ 60 fps to solve screen tearing
					renderer.ColorP = Vector4(1.0, 0.7, 0.4, 0.0);
					renderer.AddSprite( muzzleFlashes[GetRandom(muzzleFlashes.length)], weapMatrix*(Vector3(3.5, 200, 12.5)-pivot), 0.5+0.2*GetRandom() , 2.0*PiF*GetRandom());
				}
			}
		}
	}
	
	IWeaponSkin@ CreateViewRifleSkin(Renderer@ r, AudioDevice@ dev) {
		return ViewRifleSkin(r, dev);
	}
}
