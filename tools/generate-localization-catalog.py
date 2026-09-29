#!/usr/bin/env python3
"""Build editable UTF-8 language packs from recovered retail strings.

The shipped client contained eleven complete translations. This tool reads
those Python assignment modules as data, writes JSON with real Unicode code
points (never ``\\u`` escapes), and fails if a supposed retail language is
missing or incomplete. Community-only locales remain small overlays and
inherit missing keys from English at runtime.
"""

from __future__ import annotations

import argparse
import ast
import json
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class LocaleSource:
    """Metadata and optional recovered source module for one locale."""

    native_name: str
    font_asset: str = ""
    source_module: str | None = None


# These source names match aoslib/strings/__init__.py in the recovered retail
# client. Russian, Polish and Turkish deliberately use Tuffy: retail selected
# it for those languages because the decorative Latin faces lack glyphs.
LOCALES: dict[str, LocaleSource] = {
    "en": LocaleSource("English", source_module="english.py"),
    "bg": LocaleSource("Български", "fonts/Tuffy_Bold.ttf"),
    "ru": LocaleSource("Русский", "fonts/Tuffy_Bold.ttf", "russian.py"),
    "uk": LocaleSource("Українська", "fonts/Tuffy_Bold.ttf"),
    "pl": LocaleSource("Polski", "fonts/Tuffy_Bold.ttf", "polish.py"),
    "cs": LocaleSource("Čeština", "fonts/Tuffy_Bold.ttf"),
    "de": LocaleSource("Deutsch", source_module="german.py"),
    "fr": LocaleSource("Français", source_module="french.py"),
    "es": LocaleSource("Español", source_module="spanish.py"),
    "es-MX": LocaleSource("Español (México)", source_module="spanish_mexico.py"),
    "pt-BR": LocaleSource("Português (Brasil)", source_module="portuguese_brazil.py"),
    "it": LocaleSource("Italiano", source_module="italian.py"),
    "tr": LocaleSource("Türkçe", "fonts/Tuffy_Bold.ttf", "turkish.py"),
    "ja": LocaleSource(
        "日本語", "fonts/NotoSansJP-SemiBold.ttf", "japanese.py"
    ),
}


COMMUNITY_OVERLAYS: dict[str, dict[str, str]] = {
    "bg": {
        "SETTINGS": "Настройки",
        "PLAYER_PROFILE": "Профил",
        "PLAY_ONLINE": "Избери мач",
        "JOIN_MATCH": "Присъедини се",
        "CREATE_MATCH": "Създай мач",
        "WELCOME": "Добре дошъл",
        "QUIT": "Изход",
        "BACK": "Назад",
        "SELECT": "Избери",
        "CANCEL": "Отказ",
        "DONE": "Готово",
        "DEFAULTS": "По подразбиране",
        "FRIENDS": "Приятели",
        "SERVER_BROWSER": "Списък със сървъри",
        "MATCH_LOBBY": "Лоби за мач",
        "MATCH_SETTINGS": "Настройки на мача",
        "START_GAME": "Стартирай играта",
        "CHANGE_TEAM": "Смени отбор",
        "CHANGE_CLASS": "Смени клас",
        "RESUME": "Продължи",
    },
    "uk": {
        "SETTINGS": "Налаштування",
        "PLAYER_PROFILE": "Профіль гравця",
        "PLAY_ONLINE": "Обрати матч",
        "JOIN_MATCH": "Приєднатися",
        "CREATE_MATCH": "Створити матч",
        "WELCOME": "Ласкаво просимо",
        "QUIT": "Вийти",
        "BACK": "Назад",
        "SELECT": "Обрати",
        "CANCEL": "Скасувати",
        "DONE": "Готово",
        "DEFAULTS": "Типові",
        "FRIENDS": "Друзі",
        "SERVER_BROWSER": "Список серверів",
        "MATCH_LOBBY": "Лобі матчу",
        "MATCH_SETTINGS": "Налаштування матчу",
        "START_GAME": "Почати гру",
        "CHANGE_TEAM": "Змінити команду",
        "CHANGE_CLASS": "Змінити клас",
        "RESUME": "Продовжити",
    },
    "cs": {
        "SETTINGS": "Nastavení",
        "PLAYER_PROFILE": "Profil hráče",
        "PLAY_ONLINE": "Vybrat zápas",
        "JOIN_MATCH": "Připojit se",
        "CREATE_MATCH": "Vytvořit zápas",
        "WELCOME": "Vítejte",
        "QUIT": "Ukončit",
        "BACK": "Zpět",
        "SELECT": "Vybrat",
        "CANCEL": "Zrušit",
        "DONE": "Hotovo",
        "DEFAULTS": "Výchozí",
        "FRIENDS": "Přátelé",
        "SERVER_BROWSER": "Seznam serverů",
        "MATCH_LOBBY": "Herní lobby",
        "MATCH_SETTINGS": "Nastavení zápasu",
        "START_GAME": "Spustit hru",
        "CHANGE_TEAM": "Změnit tým",
        "CHANGE_CLASS": "Změnit třídu",
        "RESUME": "Pokračovat",
    },
}


# These controls were added by the revival client and therefore cannot exist
# in the 2015 retail modules. Keep this overlay intentionally small and keyed
# by the exact TextDrawCommand identifiers used by the native frontend.
CLIENT_OVERLAYS: dict[str, dict[str, str]] = {
    "en": {
        "LANGUAGE": "Language",
        "LOGOUT": "Logout",
        "PLAYER IDENTITY": "Player Identity",
        "USERNAME": "Username",
        "PASSWORD": "Password",
        "SIGN IN": "Sign In",
        "REGISTER": "Register",
        "PLAY AS GUEST": "Play as Guest",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Sign in, register, or play as guest",
        "SAVE YOUR RECOVERY CODE": "Save Your Recovery Code",
        "REQUESTS": "Requests",
        "INVITES": "Invites",
        "SEARCH PLAYER...": "Search player...",
        "ONLINE": "Online",
        "OFFLINE": "Offline",
        "LOBBY INVITATIONS": "Lobby Invitations",
        "CREATE LOBBY": "Create Lobby",
        "DECLINE": "Decline",
        "ACCEPT": "Accept",
        "FRIENDS + LOBBY": "Friends + Lobby",
    },
    "de": {
        "LANGUAGE": "Sprache",
        "LOGOUT": "Abmelden",
        "PLAYER IDENTITY": "Spieleridentität",
        "USERNAME": "Benutzername",
        "PASSWORD": "Passwort",
        "SIGN IN": "Anmelden",
        "REGISTER": "Registrieren",
        "PLAY AS GUEST": "Als Gast spielen",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Anmelden, registrieren oder als Gast spielen",
        "SAVE YOUR RECOVERY CODE": "Wiederherstellungscode speichern",
        "REQUESTS": "Anfragen",
        "INVITES": "Einladungen",
        "SEARCH PLAYER...": "Spieler suchen...",
        "ONLINE": "Online",
        "OFFLINE": "Offline",
        "LOBBY INVITATIONS": "Lobby-Einladungen",
        "CREATE LOBBY": "Lobby erstellen",
        "DECLINE": "Ablehnen",
        "ACCEPT": "Annehmen",
        "FRIENDS + LOBBY": "Freunde + Lobby",
    },
    "bg": {
        "LANGUAGE": "Език",
        "LOGOUT": "Изход от профила",
        "PLAYER IDENTITY": "Профил на играча",
        "USERNAME": "Потребителско име",
        "PASSWORD": "Парола",
        "SIGN IN": "Вход",
        "REGISTER": "Регистрация",
        "PLAY AS GUEST": "Играй като гост",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Влез, регистрирай се или играй като гост",
        "SAVE YOUR RECOVERY CODE": "Запази кода си за възстановяване",
        "REQUESTS": "Заявки",
        "INVITES": "Покани",
        "SEARCH PLAYER...": "Търси играч...",
        "ONLINE": "На линия",
        "OFFLINE": "Извън линия",
        "LOBBY INVITATIONS": "Покани за лоби",
        "CREATE LOBBY": "Създай лоби",
        "DECLINE": "Откажи",
        "ACCEPT": "Приеми",
        "FRIENDS + LOBBY": "Приятели + Лоби",
    },
    "uk": {
        "LANGUAGE": "Мова",
        "LOGOUT": "Вийти з профілю",
        "PLAYER IDENTITY": "Обліковий запис гравця",
        "USERNAME": "Ім’я користувача",
        "PASSWORD": "Пароль",
        "SIGN IN": "Увійти",
        "REGISTER": "Зареєструватися",
        "PLAY AS GUEST": "Грати як гість",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Увійдіть, зареєструйтеся або грайте як гість",
        "SAVE YOUR RECOVERY CODE": "Збережіть код відновлення",
        "REQUESTS": "Запити",
        "INVITES": "Запрошення",
        "SEARCH PLAYER...": "Знайти гравця...",
        "ONLINE": "У мережі",
        "OFFLINE": "Не в мережі",
        "LOBBY INVITATIONS": "Запрошення до лобі",
        "CREATE LOBBY": "Створити лобі",
        "DECLINE": "Відхилити",
        "ACCEPT": "Прийняти",
        "FRIENDS + LOBBY": "Друзі + Лобі",
    },
    "cs": {
        "LANGUAGE": "Jazyk",
        "LOGOUT": "Odhlásit se",
        "PLAYER IDENTITY": "Účet hráče",
        "USERNAME": "Uživatelské jméno",
        "PASSWORD": "Heslo",
        "SIGN IN": "Přihlásit se",
        "REGISTER": "Registrovat",
        "PLAY AS GUEST": "Hrát jako host",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Přihlaste se, zaregistrujte se nebo hrajte jako host",
        "SAVE YOUR RECOVERY CODE": "Uložte si kód pro obnovení",
        "REQUESTS": "Žádosti",
        "INVITES": "Pozvánky",
        "SEARCH PLAYER...": "Hledat hráče...",
        "ONLINE": "Online",
        "OFFLINE": "Offline",
        "LOBBY INVITATIONS": "Pozvánky do lobby",
        "CREATE LOBBY": "Vytvořit lobby",
        "DECLINE": "Odmítnout",
        "ACCEPT": "Přijmout",
        "FRIENDS + LOBBY": "Přátelé + Lobby",
    },
    "fr": {
        "LANGUAGE": "Langue",
        "LOGOUT": "Déconnexion",
        "PLAYER IDENTITY": "Identité du joueur",
        "USERNAME": "Nom d’utilisateur",
        "PASSWORD": "Mot de passe",
        "SIGN IN": "Connexion",
        "REGISTER": "Inscription",
        "PLAY AS GUEST": "Jouer en invité",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Connexion, inscription ou mode invité",
        "SAVE YOUR RECOVERY CODE": "Conservez votre code de récupération",
        "REQUESTS": "Demandes",
        "INVITES": "Invitations",
        "SEARCH PLAYER...": "Rechercher un joueur...",
        "ONLINE": "En ligne",
        "OFFLINE": "Hors ligne",
        "LOBBY INVITATIONS": "Invitations au salon",
        "CREATE LOBBY": "Créer un salon",
        "DECLINE": "Refuser",
        "ACCEPT": "Accepter",
        "FRIENDS + LOBBY": "Amis + Salon",
    },
    "es": {
        "LANGUAGE": "Idioma",
        "LOGOUT": "Cerrar sesión",
        "PLAYER IDENTITY": "Identidad del jugador",
        "USERNAME": "Nombre de usuario",
        "PASSWORD": "Contraseña",
        "SIGN IN": "Iniciar sesión",
        "REGISTER": "Registrarse",
        "PLAY AS GUEST": "Jugar como invitado",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Inicia sesión, regístrate o juega como invitado",
        "SAVE YOUR RECOVERY CODE": "Guarda tu código de recuperación",
        "REQUESTS": "Solicitudes",
        "INVITES": "Invitaciones",
        "SEARCH PLAYER...": "Buscar jugador...",
        "ONLINE": "En línea",
        "OFFLINE": "Desconectado",
        "LOBBY INVITATIONS": "Invitaciones a la sala",
        "CREATE LOBBY": "Crear sala",
        "DECLINE": "Rechazar",
        "ACCEPT": "Aceptar",
        "FRIENDS + LOBBY": "Amigos + Sala",
    },
    "ru": {
        "LANGUAGE": "Язык",
        "LOGOUT": "Выйти",
        "PLAYER IDENTITY": "Учётная запись игрока",
        "USERNAME": "Имя пользователя",
        "PASSWORD": "Пароль",
        "SIGN IN": "Войти",
        "REGISTER": "Регистрация",
        "PLAY AS GUEST": "Играть как гость",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Войдите, зарегистрируйтесь или играйте как гость",
        "SAVE YOUR RECOVERY CODE": "Сохраните код восстановления",
        "REQUESTS": "Запросы",
        "INVITES": "Приглашения",
        "SEARCH PLAYER...": "Найти игрока...",
        "ONLINE": "В сети",
        "OFFLINE": "Не в сети",
        "LOBBY INVITATIONS": "Приглашения в лобби",
        "CREATE LOBBY": "Создать лобби",
        "DECLINE": "Отклонить",
        "ACCEPT": "Принять",
        "FRIENDS + LOBBY": "Друзья + Лобби",
    },
    "ja": {
        "LANGUAGE": "言語",
        "LOGOUT": "ログアウト",
        "PLAYER IDENTITY": "プレイヤーID",
        "USERNAME": "ユーザー名",
        "PASSWORD": "パスワード",
        "SIGN IN": "ログイン",
        "REGISTER": "登録",
        "PLAY AS GUEST": "ゲストとしてプレイ",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "ログイン、登録、またはゲストでプレイ",
        "SAVE YOUR RECOVERY CODE": "復旧コードを保存してください",
        "REQUESTS": "リクエスト",
        "INVITES": "招待",
        "SEARCH PLAYER...": "プレイヤーを検索...",
        "ONLINE": "オンライン",
        "OFFLINE": "オフライン",
        "LOBBY INVITATIONS": "ロビーへの招待",
        "CREATE LOBBY": "ロビーを作成",
        "DECLINE": "辞退",
        "ACCEPT": "承認",
        "FRIENDS + LOBBY": "フレンド + ロビー",
    },
    "it": {
        "LANGUAGE": "Lingua",
        "LOGOUT": "Esci",
        "PLAYER IDENTITY": "Identità giocatore",
        "USERNAME": "Nome utente",
        "PASSWORD": "Password",
        "SIGN IN": "Accedi",
        "REGISTER": "Registrati",
        "PLAY AS GUEST": "Gioca come ospite",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Accedi, registrati o gioca come ospite",
        "SAVE YOUR RECOVERY CODE": "Salva il codice di recupero",
        "REQUESTS": "Richieste",
        "INVITES": "Inviti",
        "SEARCH PLAYER...": "Cerca giocatore...",
        "ONLINE": "In linea",
        "OFFLINE": "Non in linea",
        "LOBBY INVITATIONS": "Inviti alla lobby",
        "CREATE LOBBY": "Crea lobby",
        "DECLINE": "Rifiuta",
        "ACCEPT": "Accetta",
        "FRIENDS + LOBBY": "Amici + Lobby",
    },
    "pl": {
        "LANGUAGE": "Język",
        "LOGOUT": "Wyloguj",
        "PLAYER IDENTITY": "Konto gracza",
        "USERNAME": "Nazwa użytkownika",
        "PASSWORD": "Hasło",
        "SIGN IN": "Zaloguj się",
        "REGISTER": "Zarejestruj się",
        "PLAY AS GUEST": "Graj jako gość",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Zaloguj się, zarejestruj lub graj jako gość",
        "SAVE YOUR RECOVERY CODE": "Zapisz kod odzyskiwania",
        "REQUESTS": "Prośby",
        "INVITES": "Zaproszenia",
        "SEARCH PLAYER...": "Szukaj gracza...",
        "ONLINE": "Online",
        "OFFLINE": "Offline",
        "LOBBY INVITATIONS": "Zaproszenia do lobby",
        "CREATE LOBBY": "Utwórz lobby",
        "DECLINE": "Odrzuć",
        "ACCEPT": "Akceptuj",
        "FRIENDS + LOBBY": "Znajomi + Lobby",
    },
    "pt-BR": {
        "LANGUAGE": "Idioma",
        "LOGOUT": "Sair",
        "PLAYER IDENTITY": "Identidade do jogador",
        "USERNAME": "Nome de usuário",
        "PASSWORD": "Senha",
        "SIGN IN": "Entrar",
        "REGISTER": "Registrar",
        "PLAY AS GUEST": "Jogar como convidado",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Entre, registre-se ou jogue como convidado",
        "SAVE YOUR RECOVERY CODE": "Salve seu código de recuperação",
        "REQUESTS": "Solicitações",
        "INVITES": "Convites",
        "SEARCH PLAYER...": "Buscar jogador...",
        "ONLINE": "Online",
        "OFFLINE": "Offline",
        "LOBBY INVITATIONS": "Convites para lobby",
        "CREATE LOBBY": "Criar lobby",
        "DECLINE": "Recusar",
        "ACCEPT": "Aceitar",
        "FRIENDS + LOBBY": "Amigos + Lobby",
    },
    "tr": {
        "LANGUAGE": "Dil",
        "LOGOUT": "Çıkış",
        "PLAYER IDENTITY": "Oyuncu hesabı",
        "USERNAME": "Kullanıcı adı",
        "PASSWORD": "Şifre",
        "SIGN IN": "Giriş yap",
        "REGISTER": "Kayıt ol",
        "PLAY AS GUEST": "Misafir olarak oyna",
        "SIGN IN, REGISTER, OR PLAY AS GUEST": "Giriş yap, kayıt ol veya misafir olarak oyna",
        "SAVE YOUR RECOVERY CODE": "Kurtarma kodunu kaydet",
        "REQUESTS": "İstekler",
        "INVITES": "Davetler",
        "SEARCH PLAYER...": "Oyuncu ara...",
        "ONLINE": "Çevrimiçi",
        "OFFLINE": "Çevrimdışı",
        "LOBBY INVITATIONS": "Lobi davetleri",
        "CREATE LOBBY": "Lobi oluştur",
        "DECLINE": "Reddet",
        "ACCEPT": "Kabul et",
        "FRIENDS + LOBBY": "Arkadaşlar + Lobi",
    },
}


# High-traffic native-client screens which did not exist in the retail Python
# frontend.  These are intentionally complete for the first major revival
# languages instead of leaving a half-translated friends/lobby/direct-connect
# flow around otherwise complete retail translations.
MAJOR_CLIENT_OVERLAYS: dict[str, dict[str, str]] = {
    "en": {
        "ADD FRIEND": "Add Friend",
        "CREATE + INVITE": "Create + Invite",
        "STARTING MATCH...": "Starting match...",
        "WAITING FOR HOST...": "Waiting for host...",
        "JOINING MATCH...": "Joining match...",
        "RECONNECTING TO AOSPLAY...": "Reconnecting to AoSPlay...",
        "ACTION NEEDS ATTENTION": "Action Needs Attention",
        "LOBBY_DETAILS_UNAVAILABLE": "Lobby details unavailable",
        "MAP_PREVIEW_UNAVAILABLE": "Map preview unavailable",
        "NO_LOCAL_UGC_MAPS": "No local maps available",
        "SERVER_LOCATION": "Server location",
        "IP:PORT OR HOSTNAME": "IP:port or hostname",
        "HOST": "Host",
        "IN_GAME": "In game",
    },
    "bg": {
        "ADD FRIEND": "Добави приятел",
        "CREATE + INVITE": "Създай + покани",
        "STARTING MATCH...": "Стартиране на мача...",
        "WAITING FOR HOST...": "Изчакване на хоста...",
        "JOINING MATCH...": "Присъединяване към мача...",
        "RECONNECTING TO AOSPLAY...": "Повторно свързване с AoSPlay...",
        "ACTION NEEDS ATTENTION": "Необходимо е действие",
        "LOBBY_DETAILS_UNAVAILABLE": "Данните за лобито не са достъпни",
        "MAP_PREVIEW_UNAVAILABLE": "Прегледът на картата не е достъпен",
        "NO_LOCAL_UGC_MAPS": "Няма локални карти",
        "SERVER_LOCATION": "Адрес на сървъра",
        "IP:PORT OR HOSTNAME": "IP:порт или име на хост",
        "HOST": "Хост",
        "IN_GAME": "В играта",
    },
    "de": {
        "ADD FRIEND": "Freund hinzufügen",
        "CREATE + INVITE": "Erstellen + einladen",
        "STARTING MATCH...": "Spiel wird gestartet...",
        "WAITING FOR HOST...": "Auf Host warten...",
        "JOINING MATCH...": "Spiel wird beigetreten...",
        "RECONNECTING TO AOSPLAY...": "Verbindung zu AoSPlay wird wiederhergestellt...",
        "ACTION NEEDS ATTENTION": "Aktion erforderlich",
        "LOBBY_DETAILS_UNAVAILABLE": "Lobby-Details nicht verfügbar",
        "MAP_PREVIEW_UNAVAILABLE": "Kartenvorschau nicht verfügbar",
        "NO_LOCAL_UGC_MAPS": "Keine lokalen Karten verfügbar",
        "SERVER_LOCATION": "Serveradresse",
        "IP:PORT OR HOSTNAME": "IP:Port oder Hostname",
        "HOST": "Host",
        "IN_GAME": "Im Spiel",
    },
    "es": {
        "ADD FRIEND": "Añadir amigo",
        "CREATE + INVITE": "Crear + invitar",
        "STARTING MATCH...": "Iniciando partida...",
        "WAITING FOR HOST...": "Esperando al anfitrión...",
        "JOINING MATCH...": "Uniéndose a la partida...",
        "RECONNECTING TO AOSPLAY...": "Reconectando a AoSPlay...",
        "ACTION NEEDS ATTENTION": "Acción requerida",
        "LOBBY_DETAILS_UNAVAILABLE": "Detalles de la sala no disponibles",
        "MAP_PREVIEW_UNAVAILABLE": "Vista previa del mapa no disponible",
        "NO_LOCAL_UGC_MAPS": "No hay mapas locales disponibles",
        "SERVER_LOCATION": "Dirección del servidor",
        "IP:PORT OR HOSTNAME": "IP:puerto o nombre de host",
        "HOST": "Anfitrión",
        "IN_GAME": "En partida",
    },
    "fr": {
        "ADD FRIEND": "Ajouter un ami",
        "CREATE + INVITE": "Créer + inviter",
        "STARTING MATCH...": "Démarrage de la partie...",
        "WAITING FOR HOST...": "En attente de l’hôte...",
        "JOINING MATCH...": "Connexion à la partie...",
        "RECONNECTING TO AOSPLAY...": "Reconnexion à AoSPlay...",
        "ACTION NEEDS ATTENTION": "Action requise",
        "LOBBY_DETAILS_UNAVAILABLE": "Détails du salon indisponibles",
        "MAP_PREVIEW_UNAVAILABLE": "Aperçu de la carte indisponible",
        "NO_LOCAL_UGC_MAPS": "Aucune carte locale disponible",
        "SERVER_LOCATION": "Adresse du serveur",
        "IP:PORT OR HOSTNAME": "IP:port ou nom d’hôte",
        "HOST": "Hôte",
        "IN_GAME": "En jeu",
    },
    "ru": {
        "ADD FRIEND": "Добавить друга",
        "CREATE + INVITE": "Создать + пригласить",
        "STARTING MATCH...": "Запуск матча...",
        "WAITING FOR HOST...": "Ожидание хоста...",
        "JOINING MATCH...": "Подключение к матчу...",
        "RECONNECTING TO AOSPLAY...": "Повторное подключение к AoSPlay...",
        "ACTION NEEDS ATTENTION": "Требуется действие",
        "LOBBY_DETAILS_UNAVAILABLE": "Данные лобби недоступны",
        "MAP_PREVIEW_UNAVAILABLE": "Предпросмотр карты недоступен",
        "NO_LOCAL_UGC_MAPS": "Локальные карты отсутствуют",
        "SERVER_LOCATION": "Адрес сервера",
        "IP:PORT OR HOSTNAME": "IP:порт или имя хоста",
        "HOST": "Хост",
        "IN_GAME": "В игре",
    },
    "ja": {
        "ADD FRIEND": "フレンドを追加",
        "CREATE + INVITE": "作成して招待",
        "STARTING MATCH...": "マッチを開始中...",
        "WAITING FOR HOST...": "ホストを待機中...",
        "JOINING MATCH...": "マッチに参加中...",
        "RECONNECTING TO AOSPLAY...": "AoSPlayに再接続中...",
        "ACTION NEEDS ATTENTION": "操作が必要です",
        "LOBBY_DETAILS_UNAVAILABLE": "ロビー情報を取得できません",
        "MAP_PREVIEW_UNAVAILABLE": "マッププレビューを表示できません",
        "NO_LOCAL_UGC_MAPS": "ローカルマップがありません",
        "SERVER_LOCATION": "サーバーアドレス",
        "IP:PORT OR HOSTNAME": "IP:ポートまたはホスト名",
        "HOST": "ホスト",
        "IN_GAME": "ゲーム中",
    },
}


# Native status and recovery screens added after the first localization pass.
# Keep this separate from the compact identity/friends overlay above: these
# strings are a release gate for the complete connection, matchmaking and UGC
# flow, and the regression suite checks every key in the initial major packs.
NATIVE_FLOW_OVERLAYS: dict[str, dict[str, str]] = {
    "en": {
        "ASSET_PRELOAD_FAILED": "Asset preload failed",
        "COMPATIBILITY_SHADER": "Compatibility shader",
        "CONTENT_REQUIRED": "Content required",
        "ERROR_TIMEOUT": "Connection timed out",
        "KEEP_RESOLUTION_PROMPT": "Do you want to keep this resolution setting?",
        "LEADERBOARD_UNAVAILABLE": "Leaderboard unavailable",
        "MAP_CREATOR_LOBBY_INFO": "Lobby info",
        "MATCHMAKING_OFFLINE": "Matchmaking offline",
        "MATCHMAKING_READY_TO_REFRESH": "Ready to refresh",
        "MATCHMAKING_UNAVAILABLE": "Matchmaking unavailable",
        "NETWORK_UNAVAILABLE": "Network unavailable",
        "NO_LOCAL_MAPS": "No local maps found",
        "NO_MATCH_LOBBIES_FOUND": "No open matches found",
        "NO_UGC_LOBBIES_FOUND": "No available lobbies",
        "SEARCHING_FOR_CUSTOM_MATCH": "Searching for a custom match",
        "SEARCHING_FOR_MATCH_LOBBIES": "Searching for match lobbies",
        "SEARCHING_FOR_PUBLIC_MATCH": "Searching for a public match",
        "SEARCHING_FOR_UGC_LOBBIES": "Searching for map creator lobbies",
        "SELECT_A_LOCAL_MAP": "Select a local map",
        "SELECT_UGC_LOBBY": "Select a lobby",
        "SERVER_CONNECTION_FAILED": "Server connection failed",
        "SERVER_SEARCH_FAILED": "Server search failed",
        "UGC_DELETING_MAP_MESSAGE": "Deleting local map",
        "UGC_DESCRIPTION": "Create and test a custom voxel map.",
        "UGC_OPERATION_ERROR": "The operation could not be completed",
    },
    "bg": {
        "ASSET_PRELOAD_FAILED": "Зареждането на ресурсите се провали",
        "COMPATIBILITY_SHADER": "Съвместим шейдър",
        "CONTENT_REQUIRED": "Необходимо е съдържание",
        "ERROR_TIMEOUT": "Времето за свързване изтече",
        "KEEP_RESOLUTION_PROMPT": "Искате ли да запазите тази резолюция?",
        "LEADERBOARD_UNAVAILABLE": "Класацията не е достъпна",
        "MAP_CREATOR_LOBBY_INFO": "Информация за лобито",
        "MATCHMAKING_OFFLINE": "Системата за мачове е офлайн",
        "MATCHMAKING_READY_TO_REFRESH": "Готово за обновяване",
        "MATCHMAKING_UNAVAILABLE": "Системата за мачове не е достъпна",
        "NETWORK_UNAVAILABLE": "Мрежата не е достъпна",
        "NO_LOCAL_MAPS": "Няма локални карти",
        "NO_MATCH_LOBBIES_FOUND": "Няма отворени мачове",
        "NO_UGC_LOBBIES_FOUND": "Няма достъпни лобита",
        "SEARCHING_FOR_CUSTOM_MATCH": "Търсене на персонализиран мач",
        "SEARCHING_FOR_MATCH_LOBBIES": "Търсене на лобита за мач",
        "SEARCHING_FOR_PUBLIC_MATCH": "Търсене на публичен мач",
        "SEARCHING_FOR_UGC_LOBBIES": "Търсене на лобита за създаване на карти",
        "SELECT_A_LOCAL_MAP": "Изберете локална карта",
        "SELECT_UGC_LOBBY": "Изберете лоби",
        "SERVER_CONNECTION_FAILED": "Свързването със сървъра се провали",
        "SERVER_SEARCH_FAILED": "Търсенето на сървъри се провали",
        "UGC_DELETING_MAP_MESSAGE": "Изтриване на локалната карта",
        "UGC_DESCRIPTION": "Създайте и тествайте собствена вокселна карта.",
        "UGC_OPERATION_ERROR": "Операцията не можа да бъде завършена",
    },
    "de": {
        "ASSET_PRELOAD_FAILED": "Vorladen der Ressourcen fehlgeschlagen",
        "COMPATIBILITY_SHADER": "Kompatibilitäts-Shader",
        "CONTENT_REQUIRED": "Inhalt erforderlich",
        "ERROR_TIMEOUT": "Zeitüberschreitung bei der Verbindung",
        "KEEP_RESOLUTION_PROMPT": "Möchtest du diese Auflösung beibehalten?",
        "LEADERBOARD_UNAVAILABLE": "Bestenliste nicht verfügbar",
        "MAP_CREATOR_LOBBY_INFO": "Lobby-Informationen",
        "MATCHMAKING_OFFLINE": "Spielsuche ist offline",
        "MATCHMAKING_READY_TO_REFRESH": "Bereit zum Aktualisieren",
        "MATCHMAKING_UNAVAILABLE": "Spielsuche nicht verfügbar",
        "NETWORK_UNAVAILABLE": "Netzwerk nicht verfügbar",
        "NO_LOCAL_MAPS": "Keine lokalen Karten gefunden",
        "NO_MATCH_LOBBIES_FOUND": "Keine offenen Spiele gefunden",
        "NO_UGC_LOBBIES_FOUND": "Keine verfügbaren Lobbys",
        "SEARCHING_FOR_CUSTOM_MATCH": "Benutzerdefiniertes Spiel wird gesucht",
        "SEARCHING_FOR_MATCH_LOBBIES": "Spiel-Lobbys werden gesucht",
        "SEARCHING_FOR_PUBLIC_MATCH": "Öffentliches Spiel wird gesucht",
        "SEARCHING_FOR_UGC_LOBBIES": "Karteneditor-Lobbys werden gesucht",
        "SELECT_A_LOCAL_MAP": "Lokale Karte auswählen",
        "SELECT_UGC_LOBBY": "Lobby auswählen",
        "SERVER_CONNECTION_FAILED": "Serververbindung fehlgeschlagen",
        "SERVER_SEARCH_FAILED": "Serversuche fehlgeschlagen",
        "UGC_DELETING_MAP_MESSAGE": "Lokale Karte wird gelöscht",
        "UGC_DESCRIPTION": "Erstelle und teste eine eigene Voxelkarte.",
        "UGC_OPERATION_ERROR": "Der Vorgang konnte nicht abgeschlossen werden",
    },
    "es": {
        "ASSET_PRELOAD_FAILED": "Error al precargar los recursos",
        "COMPATIBILITY_SHADER": "Sombreador de compatibilidad",
        "CONTENT_REQUIRED": "Contenido necesario",
        "ERROR_TIMEOUT": "Se agotó el tiempo de conexión",
        "KEEP_RESOLUTION_PROMPT": "¿Quieres mantener esta resolución?",
        "LEADERBOARD_UNAVAILABLE": "Clasificación no disponible",
        "MAP_CREATOR_LOBBY_INFO": "Información de la sala",
        "MATCHMAKING_OFFLINE": "El emparejamiento está desconectado",
        "MATCHMAKING_READY_TO_REFRESH": "Listo para actualizar",
        "MATCHMAKING_UNAVAILABLE": "Emparejamiento no disponible",
        "NETWORK_UNAVAILABLE": "Red no disponible",
        "NO_LOCAL_MAPS": "No se encontraron mapas locales",
        "NO_MATCH_LOBBIES_FOUND": "No se encontraron partidas abiertas",
        "NO_UGC_LOBBIES_FOUND": "No hay salas disponibles",
        "SEARCHING_FOR_CUSTOM_MATCH": "Buscando una partida personalizada",
        "SEARCHING_FOR_MATCH_LOBBIES": "Buscando salas de partida",
        "SEARCHING_FOR_PUBLIC_MATCH": "Buscando una partida pública",
        "SEARCHING_FOR_UGC_LOBBIES": "Buscando salas del creador de mapas",
        "SELECT_A_LOCAL_MAP": "Selecciona un mapa local",
        "SELECT_UGC_LOBBY": "Selecciona una sala",
        "SERVER_CONNECTION_FAILED": "Error de conexión con el servidor",
        "SERVER_SEARCH_FAILED": "Error al buscar servidores",
        "UGC_DELETING_MAP_MESSAGE": "Eliminando el mapa local",
        "UGC_DESCRIPTION": "Crea y prueba un mapa de vóxeles personalizado.",
        "UGC_OPERATION_ERROR": "No se pudo completar la operación",
    },
    "fr": {
        "ASSET_PRELOAD_FAILED": "Échec du préchargement des ressources",
        "COMPATIBILITY_SHADER": "Shader de compatibilité",
        "CONTENT_REQUIRED": "Contenu requis",
        "ERROR_TIMEOUT": "Délai de connexion dépassé",
        "KEEP_RESOLUTION_PROMPT": "Voulez-vous conserver cette résolution ?",
        "LEADERBOARD_UNAVAILABLE": "Classement indisponible",
        "MAP_CREATOR_LOBBY_INFO": "Informations du salon",
        "MATCHMAKING_OFFLINE": "Le matchmaking est hors ligne",
        "MATCHMAKING_READY_TO_REFRESH": "Prêt à actualiser",
        "MATCHMAKING_UNAVAILABLE": "Matchmaking indisponible",
        "NETWORK_UNAVAILABLE": "Réseau indisponible",
        "NO_LOCAL_MAPS": "Aucune carte locale trouvée",
        "NO_MATCH_LOBBIES_FOUND": "Aucune partie ouverte trouvée",
        "NO_UGC_LOBBIES_FOUND": "Aucun salon disponible",
        "SEARCHING_FOR_CUSTOM_MATCH": "Recherche d’une partie personnalisée",
        "SEARCHING_FOR_MATCH_LOBBIES": "Recherche de salons de partie",
        "SEARCHING_FOR_PUBLIC_MATCH": "Recherche d’une partie publique",
        "SEARCHING_FOR_UGC_LOBBIES": "Recherche de salons de création de cartes",
        "SELECT_A_LOCAL_MAP": "Sélectionnez une carte locale",
        "SELECT_UGC_LOBBY": "Sélectionnez un salon",
        "SERVER_CONNECTION_FAILED": "Échec de la connexion au serveur",
        "SERVER_SEARCH_FAILED": "Échec de la recherche de serveurs",
        "UGC_DELETING_MAP_MESSAGE": "Suppression de la carte locale",
        "UGC_DESCRIPTION": "Créez et testez une carte de voxels personnalisée.",
        "UGC_OPERATION_ERROR": "L’opération n’a pas pu être terminée",
    },
    "ru": {
        "ASSET_PRELOAD_FAILED": "Не удалось загрузить ресурсы",
        "COMPATIBILITY_SHADER": "Шейдер совместимости",
        "CONTENT_REQUIRED": "Требуется содержимое",
        "ERROR_TIMEOUT": "Время ожидания подключения истекло",
        "KEEP_RESOLUTION_PROMPT": "Сохранить это разрешение экрана?",
        "LEADERBOARD_UNAVAILABLE": "Таблица лидеров недоступна",
        "MAP_CREATOR_LOBBY_INFO": "Информация о лобби",
        "MATCHMAKING_OFFLINE": "Подбор матчей отключён",
        "MATCHMAKING_READY_TO_REFRESH": "Готово к обновлению",
        "MATCHMAKING_UNAVAILABLE": "Подбор матчей недоступен",
        "NETWORK_UNAVAILABLE": "Сеть недоступна",
        "NO_LOCAL_MAPS": "Локальные карты не найдены",
        "NO_MATCH_LOBBIES_FOUND": "Открытые матчи не найдены",
        "NO_UGC_LOBBIES_FOUND": "Доступных лобби нет",
        "SEARCHING_FOR_CUSTOM_MATCH": "Поиск пользовательского матча",
        "SEARCHING_FOR_MATCH_LOBBIES": "Поиск лобби матчей",
        "SEARCHING_FOR_PUBLIC_MATCH": "Поиск публичного матча",
        "SEARCHING_FOR_UGC_LOBBIES": "Поиск лобби редактора карт",
        "SELECT_A_LOCAL_MAP": "Выберите локальную карту",
        "SELECT_UGC_LOBBY": "Выберите лобби",
        "SERVER_CONNECTION_FAILED": "Не удалось подключиться к серверу",
        "SERVER_SEARCH_FAILED": "Не удалось найти серверы",
        "UGC_DELETING_MAP_MESSAGE": "Удаление локальной карты",
        "UGC_DESCRIPTION": "Создайте и испытайте собственную воксельную карту.",
        "UGC_OPERATION_ERROR": "Не удалось завершить операцию",
    },
    "ja": {
        "ASSET_PRELOAD_FAILED": "アセットの事前読み込みに失敗しました",
        "COMPATIBILITY_SHADER": "互換シェーダー",
        "CONTENT_REQUIRED": "コンテンツが必要です",
        "ERROR_TIMEOUT": "接続がタイムアウトしました",
        "KEEP_RESOLUTION_PROMPT": "この解像度を維持しますか？",
        "LEADERBOARD_UNAVAILABLE": "ランキングを利用できません",
        "MAP_CREATOR_LOBBY_INFO": "ロビー情報",
        "MATCHMAKING_OFFLINE": "マッチメイキングはオフラインです",
        "MATCHMAKING_READY_TO_REFRESH": "更新できます",
        "MATCHMAKING_UNAVAILABLE": "マッチメイキングを利用できません",
        "NETWORK_UNAVAILABLE": "ネットワークを利用できません",
        "NO_LOCAL_MAPS": "ローカルマップが見つかりません",
        "NO_MATCH_LOBBIES_FOUND": "参加可能なマッチが見つかりません",
        "NO_UGC_LOBBIES_FOUND": "参加可能なロビーがありません",
        "SEARCHING_FOR_CUSTOM_MATCH": "カスタムマッチを検索中",
        "SEARCHING_FOR_MATCH_LOBBIES": "マッチロビーを検索中",
        "SEARCHING_FOR_PUBLIC_MATCH": "公開マッチを検索中",
        "SEARCHING_FOR_UGC_LOBBIES": "マップ作成ロビーを検索中",
        "SELECT_A_LOCAL_MAP": "ローカルマップを選択",
        "SELECT_UGC_LOBBY": "ロビーを選択",
        "SERVER_CONNECTION_FAILED": "サーバーへの接続に失敗しました",
        "SERVER_SEARCH_FAILED": "サーバーの検索に失敗しました",
        "UGC_DELETING_MAP_MESSAGE": "ローカルマップを削除中",
        "UGC_DESCRIPTION": "カスタムボクセルマップを作成してテストします。",
        "UGC_OPERATION_ERROR": "操作を完了できませんでした",
    },
}


# These settings were introduced after the retail catalog. Ship them in every
# supported locale and retain them when rebuilding packs from retail sources.
APPEARANCE_SETTINGS_OVERLAYS: dict[str, dict[str, str]] = {
    "en": {
        "SHOW_SKINS": "Show skins",
        "SHOW_OTHER_SKINS": "Show other players' skins",
        "WEAPON_MOTION": "Weapon movement",
    },
    "bg": {
        "SHOW_SKINS": "Показване на скинове",
        "SHOW_OTHER_SKINS": "Показване на скиновете на другите играчи",
        "WEAPON_MOTION": "Движение на оръжието",
    },
    "ru": {
        "SHOW_SKINS": "Показывать скины",
        "SHOW_OTHER_SKINS": "Показывать скины других игроков",
        "WEAPON_MOTION": "Движение оружия",
    },
    "uk": {
        "SHOW_SKINS": "Показувати скіни",
        "SHOW_OTHER_SKINS": "Показувати скіни інших гравців",
        "WEAPON_MOTION": "Рух зброї",
    },
    "pl": {
        "SHOW_SKINS": "Pokaż skórki",
        "SHOW_OTHER_SKINS": "Pokaż skórki innych graczy",
        "WEAPON_MOTION": "Ruch broni",
    },
    "cs": {
        "SHOW_SKINS": "Zobrazit skiny",
        "SHOW_OTHER_SKINS": "Zobrazit skiny ostatních hráčů",
        "WEAPON_MOTION": "Pohyb zbraně",
    },
    "de": {
        "SHOW_SKINS": "Skins anzeigen",
        "SHOW_OTHER_SKINS": "Skins anderer Spieler anzeigen",
        "WEAPON_MOTION": "Waffenbewegung",
    },
    "fr": {
        "SHOW_SKINS": "Afficher les apparences",
        "SHOW_OTHER_SKINS": "Afficher les apparences des autres joueurs",
        "WEAPON_MOTION": "Mouvement de l'arme",
    },
    "es": {
        "SHOW_SKINS": "Mostrar aspectos",
        "SHOW_OTHER_SKINS": "Mostrar aspectos de otros jugadores",
        "WEAPON_MOTION": "Movimiento del arma",
    },
    "es-MX": {
        "SHOW_SKINS": "Mostrar aspectos",
        "SHOW_OTHER_SKINS": "Mostrar aspectos de otros jugadores",
        "WEAPON_MOTION": "Movimiento del arma",
    },
    "pt-BR": {
        "SHOW_SKINS": "Mostrar visuais",
        "SHOW_OTHER_SKINS": "Mostrar visuais de outros jogadores",
        "WEAPON_MOTION": "Movimento da arma",
    },
    "it": {
        "SHOW_SKINS": "Mostra aspetti",
        "SHOW_OTHER_SKINS": "Mostra aspetti degli altri giocatori",
        "WEAPON_MOTION": "Movimento dell'arma",
    },
    "tr": {
        "SHOW_SKINS": "Görünümleri göster",
        "SHOW_OTHER_SKINS": "Diğer oyuncuların görünümlerini göster",
        "WEAPON_MOTION": "Silah hareketi",
    },
    "ja": {
        "SHOW_SKINS": "スキンを表示",
        "SHOW_OTHER_SKINS": "他のプレイヤーのスキンを表示",
        "WEAPON_MOTION": "武器の動き",
    },
}


# Native-only jetpack/parachute key hints (retail parity decision D4: an
# option, off by default). Retail had no such text, so ship every locale.
PARITY_HUD_OVERLAYS: dict[str, dict[str, str]] = {
    "en": {
        "ABILITY_HINTS": "Ability key hints",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Parachute deployed",
        "ABILITY_HINT_PARACHUTE_PENDING": "Parachute opens on descent",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: deploy parachute in air",
        "ABILITY_HINT_JETPACK": "{0}: thrust",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: thrust; release + land to recharge",
    },
    "bg": {
        "ABILITY_HINTS": "Подсказки за умения",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Парашутът е отворен",
        "ABILITY_HINT_PARACHUTE_PENDING": "Парашутът се отваря при спускане",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: отвори парашута във въздуха",
        "ABILITY_HINT_JETPACK": "{0}: тяга",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: тяга; пусни и кацни за презареждане",
    },
    "ru": {
        "ABILITY_HINTS": "Подсказки способностей",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Парашют раскрыт",
        "ABILITY_HINT_PARACHUTE_PENDING": "Парашют раскроется при снижении",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: раскрыть парашют в воздухе",
        "ABILITY_HINT_JETPACK": "{0}: тяга",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: тяга; отпустите и приземлитесь для перезарядки",
    },
    "uk": {
        "ABILITY_HINTS": "Підказки вмінь",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Парашут розкрито",
        "ABILITY_HINT_PARACHUTE_PENDING": "Парашут розкриється під час зниження",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: розкрити парашут у повітрі",
        "ABILITY_HINT_JETPACK": "{0}: тяга",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: тяга; відпустіть і приземліться для перезарядки",
    },
    "pl": {
        "ABILITY_HINTS": "Podpowiedzi umiejętności",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Spadochron otwarty",
        "ABILITY_HINT_PARACHUTE_PENDING": "Spadochron otworzy się przy opadaniu",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: otwórz spadochron w powietrzu",
        "ABILITY_HINT_JETPACK": "{0}: ciąg",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: ciąg; puść i wyląduj, aby naładować",
    },
    "cs": {
        "ABILITY_HINTS": "Nápověda schopností",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Padák otevřen",
        "ABILITY_HINT_PARACHUTE_PENDING": "Padák se otevře při klesání",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: otevřít padák ve vzduchu",
        "ABILITY_HINT_JETPACK": "{0}: tah",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: tah; pusťte a přistaňte pro dobití",
    },
    "de": {
        "ABILITY_HINTS": "Fähigkeitshinweise",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Fallschirm geöffnet",
        "ABILITY_HINT_PARACHUTE_PENDING": "Fallschirm öffnet sich beim Sinken",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: Fallschirm in der Luft öffnen",
        "ABILITY_HINT_JETPACK": "{0}: Schub",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: Schub; loslassen und landen zum Aufladen",
    },
    "fr": {
        "ABILITY_HINTS": "Aide des capacités",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Parachute déployé",
        "ABILITY_HINT_PARACHUTE_PENDING": "Le parachute s'ouvre en descente",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0} : déployer le parachute en l'air",
        "ABILITY_HINT_JETPACK": "{0} : poussée",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0} : poussée ; relâchez et atterrissez pour recharger",
    },
    "es": {
        "ABILITY_HINTS": "Ayudas de habilidad",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Paracaídas desplegado",
        "ABILITY_HINT_PARACHUTE_PENDING": "El paracaídas se abre al descender",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: abrir el paracaídas en el aire",
        "ABILITY_HINT_JETPACK": "{0}: empuje",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: empuje; suelta y aterriza para recargar",
    },
    "es-MX": {
        "ABILITY_HINTS": "Ayudas de habilidad",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Paracaídas desplegado",
        "ABILITY_HINT_PARACHUTE_PENDING": "El paracaídas se abre al descender",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: abrir el paracaídas en el aire",
        "ABILITY_HINT_JETPACK": "{0}: empuje",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: empuje; suelta y aterriza para recargar",
    },
    "pt-BR": {
        "ABILITY_HINTS": "Dicas de habilidade",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Paraquedas aberto",
        "ABILITY_HINT_PARACHUTE_PENDING": "O paraquedas abre na descida",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: abrir o paraquedas no ar",
        "ABILITY_HINT_JETPACK": "{0}: impulso",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: impulso; solte e aterrisse para recarregar",
    },
    "it": {
        "ABILITY_HINTS": "Suggerimenti abilità",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Paracadute aperto",
        "ABILITY_HINT_PARACHUTE_PENDING": "Il paracadute si apre in discesa",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: apri il paracadute in aria",
        "ABILITY_HINT_JETPACK": "{0}: spinta",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: spinta; rilascia e atterra per ricaricare",
    },
    "tr": {
        "ABILITY_HINTS": "Yetenek ipuçları",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "Paraşüt açıldı",
        "ABILITY_HINT_PARACHUTE_PENDING": "Paraşüt inişte açılır",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: havada paraşütü aç",
        "ABILITY_HINT_JETPACK": "{0}: itki",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: itki; bırak ve şarj için yere in",
    },
    "ja": {
        "ABILITY_HINTS": "能力のヒント",
        "ABILITY_HINT_PARACHUTE_DEPLOYED": "パラシュート展開中",
        "ABILITY_HINT_PARACHUTE_PENDING": "降下中にパラシュートが開きます",
        "ABILITY_HINT_PARACHUTE_DEPLOY": "{0}: 空中でパラシュートを開く",
        "ABILITY_HINT_JETPACK": "{0}: 推進",
        "ABILITY_HINT_JETPACK_RECHARGE": "{0}: 推進（離して着地で回復）",
    },
}


# The shipped Japanese module accidentally mixed six Simplified-Chinese
# characters into sixteen labels. The retail Japanese fonts correctly omit
# those glyphs, so preserving the typos produces visible question marks.
# Apply narrow, language-correct repairs instead of silently substituting an
# unrelated system font.
JAPANESE_TEXT_REPLACEMENTS: dict[str, str] = {
    "弹": "弾",
    "訂阅": "購読",
    "尝試": "挑戦",
    "悬柱鏡": "吊り橋",
    "道标識": "道路標識",
    "村の体別仓": "村の納屋",
    "WW2体別仓": "WW2の納屋",
}

# One recovered Turkish label contains a literal question mark inside a word,
# which is the classic symptom of the original source having crossed a legacy
# code page. Keep the repair narrow and test it like the Japanese depot fixes.
TURKISH_TEXT_REPLACEMENTS: dict[str, str] = {
    "haz?r": "hazır",
}


def _replace_all(value: str, replacements: dict[str, str]) -> str:
    """Apply a small deterministic table of recovered-source repairs."""

    for source, replacement in replacements.items():
        value = value.replace(source, replacement)
    return value


def repair_recovered_japanese(strings: dict[str, str]) -> dict[str, str]:
    """Repair the six known Simplified-Chinese intrusions in retail Japanese."""

    repaired: dict[str, str] = {}
    for key, value in strings.items():
        for source, replacement in JAPANESE_TEXT_REPLACEMENTS.items():
            value = value.replace(source, replacement)
        repaired[key] = value
    return repaired


def validate_japanese_source(strings: dict[str, str]) -> None:
    """Reject the known broken depot whose Japanese table contains Russian.

    Several preserved client folders contain a damaged ``japanese.py`` with
    156 Russian values copied into it. Loading those strings is valid UTF-8,
    so an encoding-only test cannot detect the corruption. Generation must
    fail loudly instead of producing a readable but semantically wrong pack.

    Args:
        strings: Repaired Japanese source strings.

    Raises:
        ValueError: If any Cyrillic scalar remains in the Japanese table.
    """

    polluted = sorted(
        key
        for key, value in strings.items()
        if any("\u0400" <= character <= "\u04ff" for character in value)
    )
    if polluted:
        preview = ", ".join(polluted[:5])
        raise ValueError(
            "Japanese source is polluted with Cyrillic text; use the clean "
            f"retail strings directory ({len(polluted)} affected keys: {preview})"
        )


def recovered_strings(path: Path) -> dict[str, str]:
    """Extract constant-style string assignments from one recovered module.

    Args:
        path: UTF-8 Python source module containing retail string constants.

    Returns:
        A deterministic key-sorted mapping of localization identifiers.

    Raises:
        FileNotFoundError: If the selected recovered module is absent.
        SyntaxError: If the module is no longer parseable Python source.
        ValueError: If it contains no usable string assignments.
    """

    module = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    result: dict[str, str] = {}
    for statement in module.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        # Retail also keys score reasons and award names in mixed case
        # (TDM_Kill, TC_Contend, MOST_Kills); keep every constant-style name.
        if not isinstance(target, ast.Name) or not target.id[:1].isupper():
            continue
        try:
            value = ast.literal_eval(statement.value)
        except (ValueError, TypeError, SyntaxError):
            continue
        if isinstance(value, str):
            result[target.id] = value
    if not result:
        raise ValueError(f"no localization strings recovered from {path}")
    return dict(sorted(result.items()))


def source_directory(argument: Path) -> Path:
    """Accept the historical english.py argument or the strings directory."""

    return argument.parent if argument.is_file() else argument


def main() -> None:
    """Generate all configured language files and validate retail coverage."""

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source",
        type=Path,
        required=True,
        help="Recovered aoslib/strings directory (or its english.py for compatibility)",
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    strings_root = source_directory(args.source)
    english = recovered_strings(strings_root / "english.py")
    args.output_dir.mkdir(parents=True, exist_ok=True)

    translated_count = 0
    for locale, metadata in LOCALES.items():
        if metadata.source_module is not None:
            strings = recovered_strings(strings_root / metadata.source_module)
            missing = english.keys() - strings.keys()
            if missing:
                preview = ", ".join(sorted(missing)[:5])
                raise ValueError(
                    f"{metadata.source_module} is missing {len(missing)} retail keys: "
                    f"{preview}"
                )
            translated_count += locale != "en"
        else:
            strings = COMMUNITY_OVERLAYS.get(locale, {})

        if locale == "ja":
            strings = repair_recovered_japanese(strings)
            validate_japanese_source(strings)
        elif locale == "tr":
            strings = {
                key: _replace_all(value, TURKISH_TEXT_REPLACEMENTS)
                for key, value in strings.items()
            }

        # Copy before applying revival-only keys so the parsed source mapping
        # stays immutable and can still be compared against retail coverage.
        strings = dict(strings)
        strings.update(CLIENT_OVERLAYS.get(locale, {}))
        strings.update(MAJOR_CLIENT_OVERLAYS.get(locale, {}))
        strings.update(NATIVE_FLOW_OVERLAYS.get(locale, {}))
        strings.update(APPEARANCE_SETTINGS_OVERLAYS.get(locale, {}))
        strings.update(PARITY_HUD_OVERLAYS.get(locale, {}))

        document = {
            "schema_version": 1,
            "locale": locale,
            "native_name": metadata.native_name,
            "font_asset": metadata.font_asset,
            "strings": strings,
        }
        output = args.output_dir / f"{locale}.json"
        output.write_text(
            json.dumps(document, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )

    print(
        f"wrote {len(LOCALES)} UTF-8 language files to {args.output_dir}; "
        f"{translated_count} recovered translations, {len(english)} English keys"
    )


if __name__ == "__main__":
    main()
