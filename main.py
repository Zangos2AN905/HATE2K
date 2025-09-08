import os
import random
import shutil
from collections import defaultdict
from pathlib import Path
import dearpygui.dearpygui as dpg
import struct
import re
import json
import logging

class HATE2K:
    def __init__(self):
        logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s')
        logging.info("HATE2K initialized")
        self.file_types = {
            'CharSet': ['.xyz', '.bmp', '.png'],
            'FaceSet': ['.xyz', '.bmp', '.png'],
            'ChipSet': ['.xyz', '.bmp', '.png'],
            'Picture': ['.xyz', '.bmp', '.png'],
            'Title': ['.xyz', '.bmp', '.png'],
            'Monster': ['.xyz', '.bmp', '.png'],
            'System': ['.xyz', '.bmp', '.png'],
            'Backdrop': ['.xyz', '.bmp', '.png'],
            'Battle': ['.xyz', '.bmp', '.png'],
            'Panorama': ['.xyz', '.bmp', '.png'],
            'GameOver': ['.xyz', '.bmp', '.png'],
            'Music': ['.mid', '.wav', '.mp3'],
            'Sound': ['.wav', '.mp3'],
            'Maps': ['.lmu'],
            'Dialogues': ['dialogue']
        }
        
        self.game_dir = ""
        self.shuffle_percentage = 100
        self.selected_types = set(self.file_types.keys())
        self.recent_folders = self.load_recent_folders()
        
        self.setup_gui()
    
    def setup_gui(self):
        dpg.create_context()
        
        with dpg.file_dialog(directory_selector=True, show=False, callback=self.dir_callback, tag="dir_dialog", width=700, height=400):
            dpg.add_file_extension("", color=(255, 255, 255, 255))
        
        with dpg.window(label="HATE2K - RPG Maker 2000 Corruptor", tag="main", width=500, height=600):
            dpg.add_text("Select RPG Maker 2000 game directory:")
            
            with dpg.group(horizontal=True):
                dpg.add_input_text(tag="dir_input", width=300, readonly=True)
                dpg.add_button(label="Browse", callback=lambda: dpg.show_item("dir_dialog"))
            
            if self.recent_folders:
                dpg.add_text("Recent folders:")
                with dpg.group():
                    for folder in self.recent_folders[:5]:
                        if folder:  # Check if folder is not None
                            dpg.add_button(label=os.path.basename(folder), callback=lambda s, a, folder=folder: self.load_recent_folder(folder), width=400)
            
            dpg.add_separator()
            
            dpg.add_text("Corruption Intensity:")
            dpg.add_slider_int(tag="shuffle_slider", default_value=100, min_value=1, max_value=100, 
                             format="%d%%", callback=self.update_percentage)
            
            dpg.add_separator()
            
            dpg.add_text("File Types to Corrupt:")
            
            with dpg.group(horizontal=True):
                dpg.add_button(label="Select All", callback=self.select_all)
                dpg.add_button(label="Deselect All", callback=self.deselect_all)
            
            for file_type in self.file_types:
                if file_type == 'Dialogues':
                    dpg.add_checkbox(label="Dialogues (text in maps)", 
                                   tag=f"cb_{file_type}", default_value=True, callback=self.update_selection)
                else:
                    dpg.add_checkbox(label=f"{file_type} ({', '.join(self.file_types[file_type])})", 
                                   tag=f"cb_{file_type}", default_value=True, callback=self.update_selection)
            
            dpg.add_separator()
            
            with dpg.group(horizontal=True):
                dpg.add_button(label="CORRUPT FILES", callback=self.corrupt_files, width=150, height=40)
                dpg.add_button(label="RESTORE BACKUP", callback=self.restore_backup, width=150, height=40)
            
            dpg.add_separator()
            dpg.add_text("Status:", tag="status")
        
        dpg.create_viewport(title="HATE2K", width=520, height=650)
        dpg.setup_dearpygui()
        dpg.show_viewport()
        dpg.set_primary_window("main", True)
    
    def dir_callback(self, sender, app_data):
        self.game_dir = app_data['file_path_name']
        dpg.set_value("dir_input", self.game_dir)
        dpg.set_value("status", f"Selected: {self.game_dir}")
        logging.debug(f"Directory selected: {self.game_dir}")
        self.add_recent_folder(self.game_dir)
    
    def load_recent_folder(self, folder):
        if folder and os.path.exists(folder):
            self.game_dir = folder
            dpg.set_value("dir_input", self.game_dir)
            dpg.set_value("status", f"Selected: {self.game_dir}")
    
    def load_recent_folders(self):
        try:
            with open("recent_folders.json", "r") as f:
                return json.load(f)
        except:
            return []
    
    def add_recent_folder(self, folder):
        if folder in self.recent_folders:
            self.recent_folders.remove(folder)
        self.recent_folders.insert(0, folder)
        self.recent_folders = self.recent_folders[:10]
        try:
            with open("recent_folders.json", "w") as f:
                json.dump(self.recent_folders, f)
        except:
            pass
    
    def update_percentage(self, sender, app_data):
        self.shuffle_percentage = app_data
    
    def update_selection(self, sender, app_data):
        file_type = sender.replace("cb_", "")
        if app_data:
            self.selected_types.add(file_type)
        else:
            self.selected_types.discard(file_type)
    
    def select_all(self):
        for file_type in self.file_types:
            dpg.set_value(f"cb_{file_type}", True)
        self.selected_types = set(self.file_types.keys())
    
    def deselect_all(self):
        for file_type in self.file_types:
            dpg.set_value(f"cb_{file_type}", False)
        self.selected_types.clear()
    
    def corrupt_files(self):
        if not self.game_dir or not os.path.exists(self.game_dir):
            dpg.set_value("status", "Error: Invalid directory!")
            return
        
        if not self.selected_types:
            dpg.set_value("status", "Error: No file types selected!")
            return
        
        try:
            dpg.set_value("status", "Corrupting files...")
            logging.info(f"Starting corruption process for {self.game_dir}")
            logging.debug(f"Selected file types: {self.selected_types}")
            logging.debug(f"Corruption intensity: {self.shuffle_percentage}%")
            corrupted_count = self._process_corruption()
            dpg.set_value("status", f"Success! Corrupted {corrupted_count} files. Backup created.")
            logging.info(f"Corruption completed: {corrupted_count} files corrupted")
        except Exception as e:
            dpg.set_value("status", f"Error: {str(e)}")
            logging.error(f"Corruption failed: {str(e)}")
    
    def _process_corruption(self):
        game_path = Path(self.game_dir)
        backup_path = game_path / "backup"
        backup_path.mkdir(exist_ok=True)
        
        files_by_type = defaultdict(list)
        
        # Collect files
        logging.debug("Starting file collection")
        for file_type in self.selected_types:
            if file_type == "Maps":
                for file in game_path.glob("*.lmu"):
                    files_by_type[file_type].append(file)
                    logging.debug(f"Found map file: {file.name}")
            elif file_type == "Dialogues":
                for file in game_path.glob("*.lmu"):
                    files_by_type[file_type].append(file)
                    logging.debug(f"Found dialogue file: {file.name}")
            else:
                folder_path = game_path / file_type
                if folder_path.exists():
                    for ext in self.file_types[file_type]:
                        found_files = list(folder_path.glob(f"*{ext}"))
                        files_by_type[file_type].extend(found_files)
                        logging.debug(f"Found {len(found_files)} {ext} files in {file_type}")
                else:
                    logging.debug(f"Folder not found: {folder_path}")
        
        total_corrupted = 0
        
        # Process each file type
        logging.debug(f"Processing {len(files_by_type)} file types")
        for file_type, files in files_by_type.items():
            logging.debug(f"Processing {file_type}: {len(files)} files")
            if file_type == "Dialogues":
                dialogue_count = self._corrupt_dialogues(files, backup_path)
                logging.debug(f"Corrupted {dialogue_count} dialogue files")
                total_corrupted += dialogue_count
            else:
                if len(files) < 2:
                    logging.debug(f"Skipping {file_type}: insufficient files ({len(files)})")
                    continue
                
                corrupt_count = max(2, int(len(files) * (self.shuffle_percentage / 100)))
                files_to_corrupt = random.sample(files, min(corrupt_count, len(files)))
                logging.debug(f"Corrupting {len(files_to_corrupt)} of {len(files)} {file_type} files")
                
                for file in files_to_corrupt:
                    if file_type == "Maps":
                        backup_file = backup_path / file.name
                    else:
                        type_backup = backup_path / file_type
                        type_backup.mkdir(exist_ok=True)
                        backup_file = type_backup / file.name
                    shutil.copy2(file, backup_file)
                    logging.debug(f"Backed up: {file.name}")
                
                shuffled = files_to_corrupt.copy()
                while any(f1 == f2 for f1, f2 in zip(files_to_corrupt, shuffled)):
                    random.shuffle(shuffled)
                
                temp_data = {}
                for file in files_to_corrupt:
                    with open(file, 'rb') as f:
                        temp_data[file] = f.read()
                
                for orig_file, new_file in zip(files_to_corrupt, shuffled):
                    with open(orig_file, 'wb') as f:
                        f.write(temp_data[new_file])
                    logging.debug(f"Swapped {orig_file.name} with {new_file.name}")
                
                total_corrupted += len(files_to_corrupt)
                logging.debug(f"Completed {file_type} corruption: {len(files_to_corrupt)} files")
        
        return total_corrupted
    
    def _find_dialogue_strings(self, data):
        """Find dialogue strings using 0x20 start and 0x00 end pattern"""
        dialogue_texts = []
        i = 0
        
        while i < len(data) - 2:
            # Look for dialogue start marker (0x20)
            if data[i] == 0x20:
                start_pos = i + 1  # Text starts after 0x20
                
                # Find the end marker (0x00)
                end_pos = start_pos
                while end_pos < len(data) and data[end_pos] != 0x00:
                    end_pos += 1
                
                # Check if we found a valid dialogue string
                if end_pos < len(data) and end_pos > start_pos:
                    text_bytes = data[start_pos:end_pos]
                    try:
                        text = text_bytes.decode('shift_jis', errors='ignore')
                        if self._is_actual_dialogue(text):
                            dialogue_texts.append((start_pos, end_pos - start_pos, text))
                            logging.debug(f"Found dialogue: '{text[:30]}...'")
                            i = end_pos + 1
                            continue
                    except UnicodeDecodeError:
                        pass
            i += 1
                
        return dialogue_texts
    
    def _is_actual_dialogue(self, text):
        """Check if text is actual dialogue and not system data"""
        text_clean = text.strip()
        
        # Must be at least 8 characters for dialogue
        if len(text_clean) < 8:
            return False
        
        # Count special characters and control codes
        special_chars = sum(1 for c in text_clean if ord(c) < 32 or c in '►♠♣♥♦☺☻♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼')
        
        # Skip if more than 20% special characters
        if special_chars > len(text_clean) * 0.2:
            return False
        
        # Skip if it's all numbers or basic symbols
        if re.match(r'^[0-9\s\.,;:!?\-_#$%&*()\[\]{}=+]+$', text_clean):
            return False
        
        # Skip file references
        if any(ext in text_clean.lower() for ext in ['.xyz', '.bmp', '.png', '.wav', '.mid', '.mp3', '.lmu']):
            return False
        
        # Skip common system keywords
        system_words = ['map', 'event', 'switch', 'variable', 'common', 'battle', 'item', 'gold', 'exp', 'hp', 'mp', 'atk', 'def', 'int', 'agi', 'redsky']
        if any(word in text_clean.lower() for word in system_words):
            return False
        
        # Must contain normal letters (not just symbols)
        normal_chars = sum(1 for c in text_clean if c.isalnum() or c.isspace() or c in '.,!?')
        if normal_chars < len(text_clean) * 0.6:
            return False
        
        # Must have at least 2 words or be longer than 15 chars
        if len(text_clean.split()) < 2 and len(text_clean) < 15:
            return False
            
        return True
    
    def _corrupt_dialogues(self, map_files, backup_path):
        all_texts = []
        file_data = {}
        
        # Extract dialogue text using 0x20/0x00 pattern
        for map_file in map_files:
            with open(map_file, 'rb') as f:
                data = f.read()
            
            dialogue_texts = self._find_dialogue_strings(data)
            if dialogue_texts:
                file_data[map_file] = (data, dialogue_texts)
                all_texts.extend([text for _, _, text in dialogue_texts])
                logging.debug(f"Found {len(dialogue_texts)} dialogue texts in {map_file.name}")
        
        if len(all_texts) < 2:
            logging.debug("Not enough dialogue texts found for corruption")
            return 0
        
        # Shuffle all dialogue texts
        shuffled_texts = all_texts.copy()
        random.shuffle(shuffled_texts)
        
        corrupted_count = 0
        text_index = 0
        
        # Replace dialogue text in files
        for map_file, (data, dialogue_texts) in file_data.items():
            backup_file = backup_path / map_file.name
            shutil.copy2(map_file, backup_file)
            
            new_data = bytearray(data)
            
            for pos, length, original_text in dialogue_texts:
                if text_index < len(shuffled_texts):
                    new_text = shuffled_texts[text_index]
                    new_bytes = new_text.encode('shift_jis', errors='ignore')
                    
                    # Maintain original length to preserve structure
                    if len(new_bytes) > length:
                        new_bytes = new_bytes[:length]
                    elif len(new_bytes) < length:
                        # Pad with spaces instead of null bytes to prevent cutoff
                        new_bytes += b' ' * (length - len(new_bytes))
                    
                    new_data[pos:pos + length] = new_bytes
                    logging.debug(f"Replaced dialogue: '{original_text[:20]}...' -> '{new_text[:20]}...'")
                    text_index += 1
            
            with open(map_file, 'wb') as f:
                f.write(new_data)
            
            corrupted_count += 1
        
        return corrupted_count
    
    def restore_backup(self):
        if not self.game_dir:
            dpg.set_value("status", "Error: No directory selected!")
            return
        
        backup_path = Path(self.game_dir) / "backup"
        if not backup_path.exists():
            dpg.set_value("status", "Error: No backup found!")
            return
        
        try:
            dpg.set_value("status", "Restoring backup...")
            logging.info(f"Starting backup restoration for {self.game_dir}")
            restored_count = 0
            
            for backup_file in backup_path.rglob("*"):
                if backup_file.is_file():
                    relative_path = backup_file.relative_to(backup_path)
                    target_file = Path(self.game_dir) / relative_path
                    target_file.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(backup_file, target_file)
                    restored_count += 1
            
            dpg.set_value("status", f"Success! Restored {restored_count} files.")
            logging.info(f"Backup restoration completed: {restored_count} files restored")
        except Exception as e:
            dpg.set_value("status", f"Error restoring: {str(e)}")
            logging.error(f"Backup restoration failed: {str(e)}")
    
    def run(self):
        dpg.start_dearpygui()
        dpg.destroy_context()

if __name__ == "__main__":
    app = HATE2K()
    app.run()