/*
 Confidental! Not for public use.
 
 */
 
 namespace spades {
 	class Ray {
 		float rayPos = 2.f;
 		Vector3 rayOrigin;
 		Vector3 rayDir;
 		
 		void Update(float dt) {
 			rayPos += dt * 2.f;
 		}
 	}
 
 	class RayRenderer {
 		Renderer@ r;
 		AudioDevice@ a;
 		
 		AudioChunk@ fireSound;
 		AudioChunk@ fireLocalSound;
 		
 		Ray@[]@ rays;
 		
 		Image@ img;
 		
 		RayRenderer(Renderer@ r, AudioDevice@ a) {
 			@this.r = r;
 			@this.a = a;
 			@img = r.RegisterImage("Gfx/Spotlight.tga");
 			@rays = array<spades::Ray@>();
 			
			@fireSound = a.RegisterSound
				("Scripts/Skin/SMG/laser.wav");
			@fireLocalSound = a.RegisterSound
				("Scripts/Skin/SMG/laserLocal.wav");
 		}
 		
 		void Fired(Vector3 origin, Vector3 dr, bool local) {
 			Ray ray;
 			ray.rayPos = 0.f;
 			ray.rayOrigin = origin;
 			ray.rayDir = dr;
 			rays.insertLast(ray);
 			
 			if(local) {
 			
				Vector3 org = Vector3(0.4f, -0.3f, 0.5f);
				AudioParam param;
				param.volume = 6.f;
				a.PlayLocal(fireLocalSound, org, param);
 			}else{
 				AudioParam param;
				param.volume = 9.f;
				a.Play(fireSound, origin, param);
				
 			}	
 		}
 		
 		void Update(float dt) {
 			Ray@[] rays2;
 			for(uint i = 0; i < rays.length; i++) {
 				rays[i].Update(dt);
 				if(rays[i].rayPos < 1.f) rays2.insertLast(rays[i]);
 			}
 			@rays = rays2;
 		}
 		
 		void AddToScene() {
 			for(uint i = 0; i < rays.length; i++) {
 				Ray@ ray = rays[i];
 				Vector3 p1 = ray.rayOrigin + ray.rayDir * 128.f * ray.rayPos;
 				Vector3 p2 = ray.rayOrigin + ray.rayDir * 128.f * (ray.rayPos + 0.05f);
 				
 				DynamicLightParam light;
 				light.type = spades::DynamicLightType::Point;
 				light.origin = (p1 + p2) * 0.5f;
 				light.radius = 10.f;
 				light.color = Vector3(1.f, 0.3f, 0.3f);
 				light.useLensFlare = true;
 				r.AddLight(light);
 				
 				r.Color = Vector4(1.f, .26f, .3f, 0.f);
 				r.AddLongSprite(img, p1, p2, 0.03f);
 				r.Color = Vector4(1.f, .26f, .3f, 0.f) * 0.8f;
 				r.AddLongSprite(img, p1, p2, 0.08f);
 				r.Color = Vector4(1.f, .26f, .3f, 0.f) * 0.6f;
 				r.AddLongSprite(img, p1, p2, 0.2f);
 				r.Color = Vector4(1.f, .26f, .3f, 0.f) * 0.3f;
 				r.AddLongSprite(img, p1, p2, 0.6f);
 			}
 		}
 	}
 }