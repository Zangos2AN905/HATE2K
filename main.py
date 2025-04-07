import os
import random
import shutil
from collections import defaultdict
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

class RPGShufflerGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("RPG Maker 2000 Corruptor - Main Window")
        
        # File types and their checkboxes
        self.file_types = {
            'Graphics - Charset': {'folder': 'CharSet', 'ext': ['.xyz', '.bmp', '.png'],},
            'Graphics - Faceset': {'folder': 'FaceSet', 'ext': ['.xyz', '.bmp', '.png']},
            'Graphics - Chipset': {'folder': 'ChipSet', 'ext': ['.xyz', '.bmp', '.png']},
            'Graphics - Picture': {'folder': 'Picture', 'ext': ['.xyz', '.bmp', '.png']},
            'Graphics - Title': {'folder': 'Title', 'ext': ['.xyz', '.bmp', '.png']},
            'Graphics - Monster': {'folder': 'Monster', 'ext': ['.xyz', '.bmp', '.png']},
            'Graphics - System': {'folder': 'System', 'ext': ['.xyz', '.bmp', '.png']},
            'Graphics - Backdrop': {'folder': 'Backdrop', 'ext': ['.xyz', '.bmp', '.png']},
            'Graphics - Battle': {'folder': 'Battle', 'ext': ['.xyz', '.bmp', '.png']},
            'Graphics - Panorama': {'folder': 'Panorama', 'ext': ['.xyz', '.bmp', '.png']},
            'Graphics - GameOver': {'folder': 'GameOver', 'ext': ['.xyz', '.bmp', '.png']},
            'Music': {'folder': 'Music', 'ext': ['.mid', '.wav', '.mp3']},
            'Sound' : {'folder': 'Sound', 'ext': ['.wav', '.mp3']},
            'Maps (.lmu)': '.lmu'
        }
        
        self.checkboxes = {}
        self.dir_path = tk.StringVar()
        self.shuffle_size = tk.IntVar(value=100)  # Default 100%
        
        self.create_gui()
    
    def create_gui(self):
        # Directory selection
        dir_frame = ttk.Frame(self.root, padding="5")
        dir_frame.grid(row=0, column=0, columnspan=2, sticky="ew")
        
        ttk.Label(dir_frame, text="Game Directory:").pack(side=tk.LEFT)
        ttk.Entry(dir_frame, textvariable=self.dir_path, width=50).pack(side=tk.LEFT, padx=5)
        ttk.Button(dir_frame, text="Browse", command=self.browse_directory).pack(side=tk.LEFT)
        
        # Add shuffle size slider
        size_frame = ttk.LabelFrame(self.root, text="Shuffle Percentage", padding="5")
        size_frame.grid(row=1, column=0, columnspan=2, sticky="nsew", padx=5, pady=5)
        
        self.size_label = ttk.Label(size_frame, text="100%")
        self.size_label.pack(side=tk.RIGHT)
        
        size_slider = ttk.Scale(
            size_frame,
            from_=1,
            to=100,
            variable=self.shuffle_size,
            orient=tk.HORIZONTAL,
            command=self.update_size_label
        )
        size_slider.pack(fill=tk.X, padx=5, expand=True)
        
        # Checkboxes for file types
        check_frame = ttk.LabelFrame(self.root, text="Select file types to shuffle", padding="5")
        check_frame.grid(row=2, column=0, columnspan=2, sticky="nsew", padx=5, pady=5)
        
        for i, (name, _) in enumerate(self.file_types.items()):
            var = tk.BooleanVar(value=True)
            self.checkboxes[name] = var
            ttk.Checkbutton(check_frame, text=name, variable=var).grid(row=i, column=0, sticky="w")
        
        # Buttons frame
        button_frame = ttk.Frame(self.root)
        button_frame.grid(row=3, column=0, columnspan=2, pady=10)
        ttk.Button(button_frame, text="Shuffle Files", command=self.shuffle_files).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="Restore Backup", command=self.restore_backup).pack(side=tk.LEFT, padx=5)
    
    def update_size_label(self, *args):
        self.size_label.config(text=f"{self.shuffle_size.get()}%")
    
    def browse_directory(self):
        directory = filedialog.askdirectory()
        if directory:
            self.dir_path.set(directory)
    
    def shuffle_files(self):
        directory = self.dir_path.get()
        if not os.path.exists(directory):
            messagebox.showerror("Error", "Directory not found!")
            return
        
        # Get selected extensions
        selected_extensions = []
        for name, var in self.checkboxes.items():
            if var.get():
                ext = self.file_types[name]
                if isinstance(ext, list):
                    selected_extensions.extend(ext)
                else:
                    selected_extensions.append(ext)
        
        try:
            self.shuffle_rpg_files(directory, selected_extensions)
            messagebox.showinfo("Success", "Files shuffled successfully!\nOriginal files backed up in 'backup' folder.")
        except Exception as e:
            messagebox.showerror("Error", f"An error occurred: {str(e)}")
    
    def shuffle_rpg_files(self, directory, selected_extensions):
        files_by_ext = defaultdict(list)
        
        # Scan directory and special folders
        for name, var in self.checkboxes.items():
            if not var.get():
                continue
                
            file_type = self.file_types[name]
            if isinstance(file_type, dict):  # Special folder handling
                folder_path = os.path.join(directory, file_type['folder'])
                if os.path.exists(folder_path):
                    for filename in os.listdir(folder_path):
                        ext = os.path.splitext(filename)[1].lower()
                        if isinstance(file_type['ext'], list):
                            if ext in file_type['ext']:
                                full_path = os.path.join(file_type['folder'], filename)
                                files_by_ext[file_type['folder']].append(full_path)
                        elif filename.lower().endswith(file_type['ext']):
                            full_path = os.path.join(file_type['folder'], filename)
                            files_by_ext[file_type['folder']].append(full_path)
            else:  # Regular file handling
                for filename in os.listdir(directory):
                    ext = os.path.splitext(filename)[1].lower()
                    if isinstance(file_type, list):
                        if ext in file_type:
                            files_by_ext[ext].append(filename)
                    elif ext == file_type:
                        files_by_ext[ext].append(filename)
        
        # Create backup directory
        backup_dir = os.path.join(directory, 'backup')
        os.makedirs(backup_dir, exist_ok=True)
        
        # Process each file type
        for ext, files in files_by_ext.items():
            if len(files) < 2:
                continue
                
            shuffle_count = max(2, int(len(files) * (self.shuffle_size.get() / 100)))
            files_to_shuffle = random.sample(files, shuffle_count)
            
            temp_dir = os.path.join(directory, f'temp_{ext}')
            os.makedirs(temp_dir, exist_ok=True)
            
            # Backup and shuffle selected files
            for file in files_to_shuffle:
                src = os.path.join(directory, file)
                backup_path = os.path.join(backup_dir, os.path.dirname(file))
                os.makedirs(backup_path, exist_ok=True)
                dst = os.path.join(backup_dir, file)
                shutil.copy2(src, dst)
            
            shuffled = files_to_shuffle.copy()
            while any(f1 == f2 for f1, f2 in zip(files_to_shuffle, shuffled)):
                random.shuffle(shuffled)
            
            # Apply shuffling
            for orig, shuf in zip(files_to_shuffle, shuffled):
                src = os.path.join(directory, orig)
                temp = os.path.join(temp_dir, os.path.basename(orig))
                shutil.copy2(src, temp)
            
            for orig, shuf in zip(files_to_shuffle, shuffled):
                temp = os.path.join(temp_dir, os.path.basename(shuf))
                dst = os.path.join(directory, orig)
                shutil.copy2(temp, dst)
            
            shutil.rmtree(temp_dir)
    
    def restore_backup(self):
        directory = self.dir_path.get()
        backup_dir = os.path.join(directory, 'backup')
        
        if not os.path.exists(backup_dir):
            messagebox.showerror("Error", "No backup folder found!")
            return
            
        try:
            # Recursively copy all files from backup directory back to main directory
            for root, dirs, files in os.walk(backup_dir):
                relative_path = os.path.relpath(root, backup_dir)
                for file in files:
                    src = os.path.join(root, file)
                    dst_dir = os.path.join(directory, relative_path)
                    dst = os.path.join(dst_dir, file)
                    os.makedirs(dst_dir, exist_ok=True)
                    shutil.copy2(src, dst)
            
            # Remove backup directory
            shutil.rmtree(backup_dir)
            messagebox.showinfo("Success", "Backup restored successfully!")
        except Exception as e:
            messagebox.showerror("Error", f"An error occurred while restoring: {str(e)}")

if __name__ == "__main__":
    root = tk.Tk()
    app = RPGShufflerGUI(root)
    root.mainloop()
