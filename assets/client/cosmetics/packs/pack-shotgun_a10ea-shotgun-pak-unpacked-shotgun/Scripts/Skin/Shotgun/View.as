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
	class ViewShotgunSkin: 
	IToolSkin, IViewToolSkin, IWeaponSkin,
	BasicViewWeapon {
		
		private AudioDevice@ audioDevice;
		private Model@ gunModel;
		private Model@ pumpModel;
		
		private AudioChunk@ fireSound;
		private AudioChunk@ reloadSound;
		
		ViewShotgunSkin(Renderer@ r, AudioDevice@ dev){
			super(r);
			@audioDevice = dev;
			@gunModel = renderer.RegisterModel
				("Models/Weapons/Shotgun/WeaponNoPump.kv6");
			@pumpModel = renderer.RegisterModel
				("Models/Weapons/Shotgun/Pump.kv6");
			
			@fireSound = dev.RegisterSound
				("Sounds/Weapons/Shotgun/Fire.wav");
			@reloadSound = dev.RegisterSound
				("Sounds/Weapons/Shotgun/Reload.wav");
		}
		
		void Update(float dt) {
			BasicViewWeapon::Update(dt);
		}
		
		void WeaponFired(){
			BasicViewWeapon::WeaponFired();
			
			if(!IsMuted){
				Vector3 origin = Vector3(0.4f, 9.f, 0.5f);
				AudioParam param;
				param.volume = 8.f;
				audioDevice.PlayLocal(fireSound, origin, param);
			}
		}
		
		void ReloadingWeapon() {
			if(!IsMuted){
				Vector3 origin = Vector3(0.4f, -0.3f, 0.8f);
				AudioParam param;
				param.volume = 0.2f;
				audioDevice.PlayLocal(reloadSound, origin, param);
			}
		}
		
		float GetZPos() {
			return 0.2f - AimDownSightStateSmooth * 0.01f;
		}
		
		void Draw2D() {
			if(AimDownSightStateSmooth > 0.8){
				Image@ img = renderer.RegisterImage("Gfx/shotgun.png");
				float height = renderer.ScreenHeight;
				float width = height * (800.f / 600.f); 
				renderer.Color = (Vector4(1.f, 1.f, 1.f, 1.f));
				renderer.DrawImage(img,
					AABB2((renderer.ScreenWidth - width) * 0.5f,
							(renderer.ScreenHeight - height) * 0.5f,
							width, height));
				return;
			}
			
			BasicViewWeapon::Draw2D();
		}
		
		void AddToScene() {
			if(AimDownSightStateSmooth > 0.8){
				LeftHandPosition = Vector3(6.f, 6.f, 7.f);
				RightHandPosition = Vector3(1.f, 1.f, 7.f);
				return;
			}
		
			Matrix4 mat = CreateScaleMatrix(0.027f);
			mat = GetViewWeaponMatrix() * mat;
			
			bool reloading = IsReloading;
			float reload = ReloadProgress;
			Vector3 leftHand, rightHand;
			
			leftHand = mat * Vector3(0.f, 3.f, 3.f);
			rightHand = mat * Vector3(-0.6f, -5.f, -1.f);
			
			Vector3 position1 = mat * Vector3(0.f, 3.f, 3.f);
			Vector3 position2 = mat * Vector3(0.f, 3.f, 65.f);
			
			ModelRenderParam param;
			param.matrix = eyeMatrix * mat;
			param.depthHack = true;
			renderer.AddModel(gunModel, param);
			
			// magazine/reload action
			if(reloading) {
				if(reload < 0.5f){
					// move hand to magazine
					float per = reload / 0.5f;
					leftHand = Mix(position1, position2, SmoothStep(per));
				}else if(reload < 0.7f){
					float per = reload / 0.7f;
					leftHand = Mix(position2, position1, SmoothStep(per));
				}
			}

			// motion blending parameter
			float cockFade = 1.f;
			if(reloading){
				if(reload < 0.25f ||
					ammo < (clipSize - 1)) {
					cockFade = 0.f;
				}else{
					cockFade = (reload - 0.25f) * 10.f;
					cockFade = Min(cockFade, 1.f);
				}
			}

			if(cockFade > 0.f){
				float cock = 0.f;
				float tim = 1.f - readyState;
				if(tim < 0.f){
					// might be right after reloading
					if(ammo >= clipSize && reload > 0.5f && reload < 1.f){
						tim = reload - 0.5f;
						if(tim < 0.05f){
							cock = 0.f;
						}else if(tim < 0.12f){
							cock = (tim - 0.05f) / 0.07f;
						}else if(tim < 0.26f){
							cock = 1.f;
						}else if(tim < 0.36f){
							cock = 1.f - (tim - 0.26f) / 0.1f;
						}
					}
				}else if(tim < 0.2f){
					cock = 0.f;
				}else if(tim < 0.3f){
					cock = (tim - 0.2f) / 0.1f;
				}else if(tim < 0.42f){
					cock = 1.f;
				}else if(tim < 0.52f){
					cock = 1.f - (tim - 0.42f) / 0.1f;
				}else{
					cock = 0.f;
				}

				cock *= cockFade;
				mat = mat * CreateTranslateMatrix(0.f, cock * -1.5f, 0.f);

				leftHand = Mix(leftHand,
					mat * Vector3(0.f, 4.f, 2.f), cockFade);
			}

			param.matrix = eyeMatrix * mat;
			renderer.AddModel(pumpModel, param);

			LeftHandPosition = leftHand;
			RightHandPosition = rightHand;
		}
	}
	
	IWeaponSkin@ CreateViewShotgunSkin(Renderer@ r, AudioDevice@ dev) {
		return ViewShotgunSkin(r, dev);
	}
}
